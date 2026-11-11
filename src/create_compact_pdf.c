/*
 * create_compact_pdf.c: Compact, single file erasure report for the whole host.
 *
 * Fork-specific, see FORK.md.
 *
 * One PDF per nwipe run containing only what is needed to prove the erasure:
 *   - host identity (hostname, system / chassis serial, model)
 *   - overall result
 *   - erasure parameters (method, PRNG, verification, blanking, rounds)
 *   - one table row per disk
 *   - technician / signature block
 * Organisation, customer and tag lines are printed only when they are actually
 * configured in nwipe.conf. SMART data and speed graphs are omitted. An optional company
 * logo can be embedded at build time (./configure --with-logo=FILE). Use
 * PDF_Report_Mode = FULL (or BOTH) to get the upstream reports as well.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>
#include <libconfig.h>

#include "nwipe.h"
#include "context.h"
#include "create_pdf.h"
#include "PDFGen/pdfgen.h"
#include "version.h"
#include "method.h"
#include "logging.h"
#include "options.h"
#include "prng.h"
#include "miscellaneous.h"
#include "conf.h"
#include "host_identity.h"
#include "create_compact_pdf.h"
#include "nwipe_logo.h" /* generated at build time, see src/Makefile.am */

/* Page geometry (A4 = 595 x 842 pt) */
#define CP_LEFT 40.0f
#define CP_RIGHT 555.0f
#define CP_ROW_H 13.0f
#define CP_ROW_FONT 8.0f
#define CP_FOOTER_Y 30.0f
#define CP_TABLE_BOTTOM 60.0f /* rows never go below this */
#define CP_SIGNATURE_H 80.0f /* space needed for notes + signature block */
#define CP_CONT_TABLE_TOP 790.0f /* table header y on continuation pages */

#define CP_GREEN PDF_RGB( 0x1E, 0x7B, 0x34 )
#define CP_AMBER PDF_RGB( 0xF0, 0xB0, 0x00 )
#define CP_RED PDF_RGB( 0xC6, 0x28, 0x28 )
#define CP_GRAY PDF_RGB( 0x5A, 0x5A, 0x5A )
#define CP_LIGHT PDF_RGB( 0xE6, 0xE6, 0xE6 )
#define CP_STRIPE PDF_RGB( 0xF5, 0xF5, 0xF5 )

typedef enum { CP_STATUS_OK = 0, CP_STATUS_WARN = 1, CP_STATUS_FAIL = 2 } cp_status_t;

typedef struct
{
    const char* title;
    float x;
    float w;
} cp_column_t;

/* Column layout, x + w of the last column == CP_RIGHT.
 * HPA/DCO is deliberately not shown (management report); it is still in the log
 * and in the FULL upstream reports. */
static const cp_column_t cp_cols[] = { { "Device", 40, 62 },
                                       { "Model", 102, 160 },
                                       { "Serial", 262, 100 },
                                       { "Size", 362, 50 },
                                       { "Duration", 412, 48 },
                                       { "Errors", 460, 38 },
                                       { "Status", 498, 57 } };
#define CP_NCOLS ( sizeof( cp_cols ) / sizeof( cp_cols[0] ) )

typedef struct
{
    struct pdf_doc* pdf;
    struct pdf_object** pages;
    size_t page_count;
} cp_doc_t;

/* ------------------------------------------------------------------------- */
/* Small drawing helpers                                                      */

static void cp_text( cp_doc_t* doc,
                     struct pdf_object* pg,
                     const char* font,
                     const char* txt,
                     float size,
                     float x,
                     float y,
                     uint32_t colour )
{
    if( txt == NULL || txt[0] == '\0' )
    {
        return;
    }
    pdf_set_font( doc->pdf, font );
    pdf_add_text( doc->pdf, pg, txt, size, x, y, colour );
}

static float cp_width( cp_doc_t* doc, const char* font, const char* txt, float size )
{
    float w = 0;
    if( pdf_get_font_text_width( doc->pdf, font, txt, size, &w ) < 0 )
    {
        w = (float) strlen( txt ) * size * 0.6f; /* rough fallback */
    }
    return w;
}

/* Copy `in` to `out`, shortening it with ".." so that it fits into max_w points. */
static void cp_fit( cp_doc_t* doc, const char* font, const char* in, float size, float max_w, char* out, size_t len )
{
    char tmp[256];
    size_t n;

    snprintf( out, len, "%s", ( in != NULL ) ? in : "" );
    if( cp_width( doc, font, out, size ) <= max_w )
    {
        return;
    }
    n = strlen( out );
    while( n > 0 )
    {
        out[--n] = '\0';
        snprintf( tmp, sizeof( tmp ), "%s..", out );
        if( cp_width( doc, font, tmp, size ) <= max_w )
        {
            snprintf( out, len, "%s", tmp );
            return;
        }
    }
}

/* Label in gray, value in bold black, value truncated to value_w. */
static void cp_field( cp_doc_t* doc,
                      struct pdf_object* pg,
                      const char* label,
                      const char* value,
                      float x,
                      float value_x,
                      float value_w,
                      float y )
{
    char buf[256];

    cp_text( doc, pg, "Helvetica", label, 9, x, y, CP_GRAY );
    cp_fit( doc, "Helvetica-Bold", ( value != NULL && value[0] != '\0' ) ? value : "Unknown", 9, value_w, buf, sizeof( buf ) );
    cp_text( doc, pg, "Helvetica-Bold", buf, 9, value_x, y, PDF_BLACK );
}

static struct pdf_object* cp_new_page( cp_doc_t* doc )
{
    struct pdf_object* pg;
    struct pdf_object** tmp;

    pg = pdf_append_page( doc->pdf );
    if( pg == NULL )
    {
        return NULL;
    }
    tmp = realloc( doc->pages, ( doc->page_count + 1 ) * sizeof( *tmp ) );
    if( tmp == NULL )
    {
        return NULL;
    }
    doc->pages = tmp;
    doc->pages[doc->page_count++] = pg;
    return pg;
}

static void cp_table_header( cp_doc_t* doc, struct pdf_object* pg, float y )
{
    size_t i;

    pdf_add_filled_rectangle( doc->pdf, pg, CP_LEFT, y - 4, CP_RIGHT - CP_LEFT, CP_ROW_H + 1, 0, CP_LIGHT, PDF_TRANSPARENT );
    for( i = 0; i < CP_NCOLS; i++ )
    {
        cp_text( doc, pg, "Helvetica-Bold", cp_cols[i].title, CP_ROW_FONT, cp_cols[i].x + 2, y, PDF_BLACK );
    }
}

/* ------------------------------------------------------------------------- */
/* Data helpers                                                               */

/* Returns a config string or NULL when unset / still the "Not Applicable" default. */
static const char* cp_conf_str( const char* group, const char* name )
{
    extern config_t nwipe_cfg;
    config_setting_t* setting;
    const char* value = NULL;

    setting = config_lookup( &nwipe_cfg, group );
    if( setting != NULL && config_setting_lookup_string( setting, name, &value ) && value != NULL && value[0] != '\0'
        && strncmp( value, "Not Applicable", 14 ) != 0 )
    {
        return value;
    }
    return NULL;
}

static cp_status_t cp_disk_status( nwipe_context_t* c, const char** text )
{
    if( c->secure_erase_orchestration == NWIPE_SECURE_ERASE_ORCHESTRATION_STANDALONE )
    {
        if( c->secure_erase_status == NWIPE_SECURE_ERASE_STATUS_SUCCESS )
        {
            *text = "ERASED";
            return CP_STATUS_OK;
        }
        *text = "FAILED";
        return CP_STATUS_FAIL;
    }

    if( strcmp( c->wipe_status_txt, "ERASED" ) == 0 )
    {
        /* Unlike the upstream certificate, HPA/DCO state is ignored here: behind RAID
         * controllers (e.g. Dell PERC) hdparm cannot query it, so every disk would be
         * flagged "Unknown" although the whole addressable device was wiped and verified.
         * Only I/O retries downgrade the result to a warning. */
        if( c->io_retries == 0 )
        {
            *text = "ERASED";
            return CP_STATUS_OK;
        }
        *text = "ERASED (!)";
        return CP_STATUS_WARN;
    }

    *text = ( c->wipe_status_txt[0] != '\0' ) ? c->wipe_status_txt : "UNKNOWN";
    return CP_STATUS_FAIL;
}

static const char* cp_prng_label( void )
{
    if( nwipe_options.method == &nwipe_zero || nwipe_options.method == &nwipe_one
        || nwipe_options.method == &nwipe_verify_zero || nwipe_options.method == &nwipe_verify_one )
    {
        return "N/A (no random data)";
    }
    if( nwipe_options.prng != NULL && nwipe_options.prng->label != NULL )
    {
        return nwipe_options.prng->label;
    }
    return "Unknown";
}

static const char* cp_verify_label( void )
{
    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_LAST:
            return "Last pass";
        case NWIPE_VERIFY_ALL:
            return "All passes";
        case NWIPE_VERIFY_NONE:
        default:
            return "None";
    }
}

static void cp_time_text( time_t t, const char* fmt, char* out, size_t len )
{
    struct tm* p;

    out[0] = '\0';
    if( t == 0 )
    {
        return;
    }
    p = localtime( &t );
    if( p != NULL )
    {
        strftime( out, len, fmt, p );
    }
}

static const char* cp_skip_spaces( const char* s )
{
    if( s == NULL )
    {
        return "";
    }
    while( *s == ' ' )
    {
        s++;
    }
    return s;
}

/* ------------------------------------------------------------------------- */

int create_compact_pdf( nwipe_thread_data_ptr_t* ptrx )
{
    nwipe_context_t** c = ptrx->c;
    nwipe_misc_thread_data_t* d = ptrx->nwipe_misc_thread_data;
    int ndisks = d->nwipe_selected;

    cp_doc_t doc = { NULL, NULL, 0 };
    struct pdf_object* pg;
    nwipe_host_identity_t id;
    struct pdf_info info = { .creator = "nwipe",
                             .producer = "https://github.com/martijnvanbrummelen/nwipe",
                             .title = "Disk Erasure Report",
                             .author = "Nwipe",
                             .subject = "Disk Erasure Report",
                             .date = "Today" };

    char buf[512];
    char buf2[512];
    char model[2 * DMIDECODE_RESULT_LENGTH + 2];
    char started[32], finished[32], file_time[32];
    char fname_host[NWIPE_HOSTNAME_LENGTH], fname_serial[NWIPE_DMI_SERIAL_LENGTH];
    char filename[FILENAME_MAX];
    char* method_label;
    const char* status_txt;
    const char *org, *customer, *tag = NULL, *tech;
    time_t first_start = 0, last_end = 0;
    cp_status_t overall = CP_STATUS_OK, st;
    int any_warn = 0, any_retries = 0;
    float y, w;
    uint32_t fill, colour;
    int i;
    size_t k;
    int ret = 0;

    nwipe_host_identity_get( &id, nwipe_options.quiet );

    for( i = 0; i < ndisks; i++ )
    {
        if( c[i]->start_time != 0 && ( first_start == 0 || c[i]->start_time < first_start ) )
            first_start = c[i]->start_time;
        if( c[i]->end_time > last_end )
            last_end = c[i]->end_time;
        st = cp_disk_status( c[i], &status_txt );
        if( st > overall )
            overall = st;
        if( st == CP_STATUS_WARN )
            any_warn = 1;
        if( c[i]->io_retries != 0 )
            any_retries = 1;
    }
    if( last_end == 0 )
    {
        last_end = time( NULL );
    }

    doc.pdf = pdf_create( PDF_A4_WIDTH, PDF_A4_HEIGHT, &info );
    if( doc.pdf == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "create_compact_pdf(): pdf_create() failed" );
        return -1;
    }
    pg = cp_new_page( &doc );
    if( pg == NULL )
    {
        ret = -1;
        goto cleanup;
    }

    /* ---- Logo (optional, embedded at build time) + title + overall result badge ---- */
    {
        float title_x = CP_LEFT;
#ifdef NWIPE_HAVE_LOGO
        /* Fit the logo into a 120 x 40 pt box at the top left, keeping its aspect ratio.
         * A broken logo only costs the logo, never the report. */
        struct pdf_img_info li;
        char lerr[128] = "";
        memset( &li, 0, sizeof( li ) );
        if( pdf_parse_image_header( &li, nwipe_logo_data, sizeof( nwipe_logo_data ), lerr, sizeof( lerr ) ) == 0
            && li.width > 0 && li.height > 0 )
        {
            float lw = 120.0f;
            float lh = 120.0f * (float) li.height / (float) li.width;
            if( lh > 40.0f )
            {
                lh = 40.0f;
                lw = 40.0f * (float) li.width / (float) li.height;
            }
            if( pdf_add_image_data( doc.pdf,
                                    pg,
                                    CP_LEFT,
                                    782.0f + ( 40.0f - lh ) / 2,
                                    lw,
                                    lh,
                                    nwipe_logo_data,
                                    sizeof( nwipe_logo_data ) )
                >= 0 )
            {
                title_x = CP_LEFT + lw + 12.0f;
            }
            else
            {
                int err = 0;
                const char* msg = pdf_get_err( doc.pdf, &err );
                nwipe_log( NWIPE_LOG_WARNING, "Compact PDF: logo not added: %s", msg ? msg : "unknown error" );
                pdf_clear_err( doc.pdf );
            }
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING, "Compact PDF: logo not added: %s", lerr[0] ? lerr : "bad image" );
        }
#endif
        cp_text( &doc, pg, "Helvetica-Bold", "Disk Erasure Report", 18, title_x, 795, PDF_BLACK );
    }

    switch( overall )
    {
        case CP_STATUS_OK:
            fill = CP_GREEN;
            colour = PDF_WHITE;
            snprintf( buf, sizeof( buf ), "ERASED" );
            break;
        case CP_STATUS_WARN:
            fill = CP_AMBER;
            colour = PDF_BLACK;
            snprintf( buf, sizeof( buf ), "ERASED (!)" );
            break;
        default:
            fill = CP_RED;
            colour = PDF_WHITE;
            snprintf( buf, sizeof( buf ), "FAILED" );
            break;
    }
    pdf_add_filled_rectangle( doc.pdf, pg, 385, 787, CP_RIGHT - 385, 26, 0, fill, PDF_TRANSPARENT );
    w = cp_width( &doc, "Helvetica-Bold", buf, 11 );
    cp_text( &doc, pg, "Helvetica-Bold", buf, 11, 385 + ( CP_RIGHT - 385 - w ) / 2, 796, colour );

    pdf_add_line( doc.pdf, pg, CP_LEFT, 778, CP_RIGHT, 778, 1.5f, PDF_BLACK );

    /* ---- Host identity ---- */
    if( d->dmidecode_system_manufacturer[0] != '\0' || d->dmidecode_system_product_name[0] != '\0' )
    {
        snprintf( model,
                  sizeof( model ),
                  "%s %s",
                  d->dmidecode_system_manufacturer,
                  d->dmidecode_system_product_name );
    }
    else
    {
        /* dmidecode gave nothing: fall back to sysfs */
        char vendor[DMIDECODE_RESULT_LENGTH], product[DMIDECODE_RESULT_LENGTH];
        nwipe_read_dmi_field( "/sys/class/dmi/id/sys_vendor", vendor, sizeof( vendor ) );
        nwipe_read_dmi_field( "/sys/class/dmi/id/product_name", product, sizeof( product ) );
        snprintf( model, sizeof( model ), "%s %s", vendor, product );
    }
    /* keep printable chars only, collapse/trim white space (dmidecode may return blanks) */
    {
        size_t r, n = 0;
        int space = 1;
        for( r = 0; model[r] != '\0'; r++ )
        {
            unsigned char ch = (unsigned char) model[r];
            if( ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' )
            {
                if( !space )
                    model[n++] = ' ';
                space = 1;
            }
            else if( ch >= 0x21 && ch < 0x7f )
            {
                model[n++] = (char) ch;
                space = 0;
            }
        }
        while( n > 0 && model[n - 1] == ' ' )
            n--;
        model[n] = '\0';
        if( n == 0 )
            snprintf( model, sizeof( model ), "Unknown" );
    }
    cp_time_text( first_start, "%Y-%m-%d %H:%M:%S", started, sizeof( started ) );
    cp_time_text( last_end, "%Y-%m-%d %H:%M:%S", finished, sizeof( finished ) );

    cp_field( &doc, pg, "Hostname:", id.hostname, CP_LEFT, 105, 190, 762 );
    cp_field( &doc, pg, "System S/N:", id.system_serial, 300, 370, 185, 762 );
    cp_field( &doc, pg, "Model:", model, CP_LEFT, 105, 190, 748 );
    cp_field( &doc, pg, "Chassis S/N:", id.chassis_serial, 300, 370, 185, 748 );
    cp_field( &doc, pg, "Started:", started, CP_LEFT, 105, 190, 734 );
    cp_field( &doc, pg, "Finished:", finished, 300, 370, 185, 734 );

    pdf_add_line( doc.pdf, pg, CP_LEFT, 725, CP_RIGHT, 725, 0.5f, CP_GRAY );

    /* ---- Erasure parameters, 3 columns x 2 rows ---- */
    method_label = nwipe_method_label_with_direction();
    cp_field( &doc,
              pg,
              "Method:",
              method_label != NULL ? method_label : nwipe_method_label( nwipe_options.method ),
              CP_LEFT,
              90,
              120,
              710 );
    free( method_label );
    cp_field( &doc, pg, "PRNG:", cp_prng_label(), 215, 255, 130, 710 );
    cp_field( &doc, pg, "Verify:", cp_verify_label(), 395, 440, 115, 710 );

    cp_field( &doc, pg, "Blanking:", nwipe_options.noblank ? "None" : "Zeros", CP_LEFT, 90, 120, 696 );
    snprintf( buf, sizeof( buf ), "%d", nwipe_options.rounds );
    cp_field( &doc, pg, "Rounds:", buf, 215, 255, 130, 696 );
    snprintf( buf, sizeof( buf ), "%d", ndisks );
    cp_field( &doc, pg, "Disks:", buf, 395, 440, 115, 696 );

    y = 687;

    /* ---- Optional: organisation / customer / tag, only if configured ---- */
    org = cp_conf_str( "Organisation_Details", "Business_Name" );
    customer = cp_conf_str( "Selected_Customer", "Customer_Name" );
    if( nwipe_options.PDFtag )
    {
        tag = cp_conf_str( "PDF_Certificate", "User_Defined_Tag" );
        if( tag != NULL && strcasecmp( tag, "Empty Tag" ) == 0 )
            tag = NULL; /* unset default from conf.c */
    }
    if( org != NULL || customer != NULL || tag != NULL )
    {
        /* only print what is actually configured, packed left to right */
        float fx = CP_LEFT;
        pdf_add_line( doc.pdf, pg, CP_LEFT, y, CP_RIGHT, y, 0.5f, CP_GRAY );
        y -= 15;
        if( org != NULL )
        {
            cp_field( &doc, pg, "Organisation:", org, fx, fx + 60, 110, y );
            fx += 175;
        }
        if( customer != NULL )
        {
            cp_field( &doc, pg, "Customer:", customer, fx, fx + 47, 125, y );
            fx += 180;
        }
        if( tag != NULL )
        {
            cp_field( &doc, pg, "Tag:", tag, fx, fx + 25, 130, y );
        }
        y -= 9;
    }

    pdf_add_line( doc.pdf, pg, CP_LEFT, y, CP_RIGHT, y, 1.5f, PDF_BLACK );

    /* ---- Disk table ---- */
    y -= 18;
    cp_table_header( &doc, pg, y );

    for( i = 0; i < ndisks; i++ )
    {
        nwipe_context_t* ci = c[i];
        u64 errors;

        y -= CP_ROW_H;
        if( y < CP_TABLE_BOTTOM )
        {
            pg = cp_new_page( &doc );
            if( pg == NULL )
            {
                ret = -1;
                goto cleanup;
            }
            snprintf( buf, sizeof( buf ), "Disk Erasure Report - %s (continued)", id.hostname[0] ? id.hostname : "host" );
            cp_text( &doc, pg, "Helvetica-Bold", buf, 11, CP_LEFT, 812, PDF_BLACK );
            y = CP_CONT_TABLE_TOP;
            cp_table_header( &doc, pg, y );
            y -= CP_ROW_H;
        }

        if( i % 2 )
        {
            pdf_add_filled_rectangle(
                doc.pdf, pg, CP_LEFT, y - 4, CP_RIGHT - CP_LEFT, CP_ROW_H, 0, CP_STRIPE, PDF_TRANSPARENT );
        }

        /* Device */
        cp_fit( &doc, "Helvetica", ci->device_name, CP_ROW_FONT, cp_cols[0].w - 4, buf, sizeof( buf ) );
        cp_text( &doc, pg, "Helvetica", buf, CP_ROW_FONT, cp_cols[0].x + 2, y, PDF_BLACK );

        /* Model */
        cp_fit( &doc,
                "Helvetica",
                ( ci->device_model != NULL && ci->device_model[0] ) ? ci->device_model : "Unknown",
                CP_ROW_FONT,
                cp_cols[1].w - 4,
                buf,
                sizeof( buf ) );
        cp_text( &doc, pg, "Helvetica", buf, CP_ROW_FONT, cp_cols[1].x + 2, y, PDF_BLACK );

        /* Serial */
        cp_fit( &doc,
                "Helvetica",
                ci->device_serial_no[0] ? ci->device_serial_no : "Unknown",
                CP_ROW_FONT,
                cp_cols[2].w - 4,
                buf,
                sizeof( buf ) );
        cp_text( &doc, pg, "Helvetica", buf, CP_ROW_FONT, cp_cols[2].x + 2, y, PDF_BLACK );

        /* Size */
        cp_fit( &doc,
                "Helvetica",
                cp_skip_spaces( ci->device_size_text ),
                CP_ROW_FONT,
                cp_cols[3].w - 4,
                buf,
                sizeof( buf ) );
        cp_text( &doc, pg, "Helvetica", buf, CP_ROW_FONT, cp_cols[3].x + 2, y, PDF_BLACK );

        /* Duration */
        cp_text( &doc, pg, "Helvetica", ci->duration_str, CP_ROW_FONT, cp_cols[4].x + 2, y, PDF_BLACK );

        /* Errors = pass + sync + verify, "+nR" = I/O retries */
        errors = ci->pass_errors + ci->fsyncdata_errors + ci->verify_errors;
        if( ci->io_retries != 0 )
            snprintf( buf, sizeof( buf ), "%llu+%lluR", (unsigned long long) errors, (unsigned long long) ci->io_retries );
        else
            snprintf( buf, sizeof( buf ), "%llu", (unsigned long long) errors );
        cp_text( &doc,
                 pg,
                 "Helvetica",
                 buf,
                 CP_ROW_FONT,
                 cp_cols[5].x + 2,
                 y,
                 ( errors != 0 || ci->io_retries != 0 ) ? CP_RED : PDF_BLACK );

        /* Status */
        st = cp_disk_status( ci, &status_txt );
        colour = ( st == CP_STATUS_OK ) ? CP_GREEN : ( st == CP_STATUS_WARN ) ? PDF_RGB( 0xB0, 0x6A, 0x00 ) : CP_RED;
        cp_text( &doc, pg, "Helvetica-Bold", status_txt, CP_ROW_FONT, cp_cols[6].x + 2, y, colour );
    }

    pdf_add_line( doc.pdf, pg, CP_LEFT, y - 5, CP_RIGHT, y - 5, 0.5f, CP_GRAY );

    /* ---- Notes + signature block, new page if it doesn't fit ---- */
    if( y - CP_SIGNATURE_H < CP_TABLE_BOTTOM )
    {
        pg = cp_new_page( &doc );
        if( pg == NULL )
        {
            ret = -1;
            goto cleanup;
        }
        snprintf( buf, sizeof( buf ), "Disk Erasure Report - %s (continued)", id.hostname[0] ? id.hostname : "host" );
        cp_text( &doc, pg, "Helvetica-Bold", buf, 11, CP_LEFT, 812, PDF_BLACK );
        y = 800;
    }

    y -= 18;
    if( any_warn )
    {
        cp_text( &doc,
                 pg,
                 "Helvetica",
                 "(!) Erased and verified, but the disk needed I/O retries during the wipe.",
                 7.5f,
                 CP_LEFT,
                 y,
                 CP_GRAY );
        y -= 10;
    }
    if( any_retries )
    {
        cp_text( &doc,
                 pg,
                 "Helvetica",
                 "Errors column: pass + sync + verify errors; nR = number of I/O retries.",
                 7.5f,
                 CP_LEFT,
                 y,
                 CP_GRAY );
        y -= 10;
    }

    /* Signature block at a fixed position near the bottom of the last page */
    tech = cp_conf_str( "Organisation_Details", "Op_Tech_Name" );
    cp_text( &doc, pg, "Helvetica", "Technician:", 9, CP_LEFT, 90, CP_GRAY );
    if( tech != NULL )
    {
        cp_fit( &doc, "Helvetica-Bold", tech, 9, 120, buf, sizeof( buf ) );
        cp_text( &doc, pg, "Helvetica-Bold", buf, 9, 95, 92, PDF_BLACK );
    }
    pdf_add_line( doc.pdf, pg, 92, 87, 220, 87, 0.5f, PDF_BLACK );
    cp_text( &doc, pg, "Helvetica", "Signature:", 9, 235, 90, CP_GRAY );
    pdf_add_line( doc.pdf, pg, 283, 87, 430, 87, 0.5f, PDF_BLACK );
    cp_text( &doc, pg, "Helvetica", "Date:", 9, 445, 90, CP_GRAY );
    pdf_add_line( doc.pdf, pg, 470, 87, CP_RIGHT, 87, 0.5f, PDF_BLACK );

    /* ---- Footer on every page ---- */
    for( k = 0; k < doc.page_count; k++ )
    {
        pdf_add_line( doc.pdf, doc.pages[k], CP_LEFT, 45, CP_RIGHT, 45, 0.5f, CP_GRAY );
        snprintf( buf, sizeof( buf ), "nwipe %s  -  %s", version_string, id.hostname[0] ? id.hostname : "unknown host" );
        cp_fit( &doc, "Helvetica", buf, 7.5f, 380, buf2, sizeof( buf2 ) );
        cp_text( &doc, doc.pages[k], "Helvetica", buf2, 7.5f, CP_LEFT, CP_FOOTER_Y, CP_GRAY );
        snprintf( buf, sizeof( buf ), "Page %zu of %zu", k + 1, doc.page_count );
        w = cp_width( &doc, "Helvetica", buf, 7.5f );
        cp_text( &doc, doc.pages[k], "Helvetica", buf, 7.5f, CP_RIGHT - w, CP_FOOTER_Y, CP_GRAY );
    }

    /* ---- Save ---- */
    cp_time_text( last_end, "%Y-%m-%d_%H-%M-%S", file_time, sizeof( file_time ) );
    nwipe_host_identity_filename_safe( id.hostname, fname_host, sizeof( fname_host ) );
    nwipe_host_identity_filename_safe( id.system_serial[0] ? id.system_serial : id.chassis_serial,
                                       fname_serial,
                                       sizeof( fname_serial ) );
    snprintf( filename,
              sizeof( filename ),
              "%s/nwipe_compact_report_%s_host-%s_SN-%s.pdf",
              nwipe_options.PDFreportpath,
              file_time,
              fname_host,
              fname_serial );

    if( pdf_save( doc.pdf, filename ) < 0 )
    {
        int err = 0;
        nwipe_log( NWIPE_LOG_ERROR,
                   "create_compact_pdf(): failed to save %s: %s",
                   filename,
                   pdf_get_err( doc.pdf, &err ) );
        ret = -1;
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "Compact PDF report saved: %s", filename );
    }

cleanup:
    pdf_destroy( doc.pdf );
    free( doc.pages );
    return ret;
}
