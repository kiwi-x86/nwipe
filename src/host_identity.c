/*
 *  host_identity.c: Host identification helpers (hostname, DMI serials).
 *
 *  Fork-specific. See host_identity.h and FORK.md.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "host_identity.h"

#define NWIPE_ANONYMIZED_VALUE "XXXXXXXXXXXXXXX"

/* The kernel parses the SMBIOS tables at boot and exposes these as plain
 * root-readable files, so no dmidecode subprocess is required. On VMs and
 * inside containers the file is typically absent, empty or a placeholder. */
void nwipe_read_dmi_field( const char* path, char* out, size_t len )
{
    static const char* placeholders[] = { "None",
                                          "Not Specified",
                                          "To Be Filled By O.E.M.",
                                          "System Serial Number",
                                          "Chassis Serial Number",
                                          "Default string",
                                          "0123456789",
                                          NULL };
    size_t i;
    FILE* fp;

    if( out == NULL || len == 0 )
    {
        return;
    }
    out[0] = '\0';

    fp = fopen( path, "r" );
    if( fp != NULL )
    {
        if( fgets( out, (int) len, fp ) == NULL )
        {
            out[0] = '\0';
        }
        fclose( fp );
    }

    /* Strip the trailing newline and any trailing white space. */
    out[strcspn( out, "\r\n" )] = '\0';
    i = strlen( out );
    while( i > 0 && isspace( (unsigned char) out[i - 1] ) )
    {
        out[--i] = '\0';
    }

    for( i = 0; placeholders[i] != NULL; i++ )
    {
        if( strcasecmp( out, placeholders[i] ) == 0 )
        {
            out[0] = '\0';
            break;
        }
    }
}

static void anonymize_field( char* field, size_t len )
{
    if( field[0] != '\0' )
    {
        snprintf( field, len, "%s", NWIPE_ANONYMIZED_VALUE );
    }
}

void nwipe_host_identity_get( nwipe_host_identity_t* id, int anonymize )
{
    memset( id, 0, sizeof( *id ) );

    if( gethostname( id->hostname, sizeof( id->hostname ) ) != 0 )
    {
        id->hostname[0] = '\0';
    }
    /* gethostname() may not NUL terminate on truncation. */
    id->hostname[sizeof( id->hostname ) - 1] = '\0';

    /* Supermicro (and other barebones) systems serialise the chassis and the
     * system board independently, so the two fields commonly differ; Dell
     * programs the same Service Tag into both. */
    nwipe_read_dmi_field( "/sys/class/dmi/id/product_serial", id->system_serial, sizeof( id->system_serial ) );
    nwipe_read_dmi_field( "/sys/class/dmi/id/chassis_serial", id->chassis_serial, sizeof( id->chassis_serial ) );

    if( anonymize )
    {
        anonymize_field( id->hostname, sizeof( id->hostname ) );
        anonymize_field( id->system_serial, sizeof( id->system_serial ) );
        anonymize_field( id->chassis_serial, sizeof( id->chassis_serial ) );
    }
}

void nwipe_host_identity_filename_safe( const char* in, char* out, size_t len )
{
    size_t i;

    if( out == NULL || len == 0 )
    {
        return;
    }

    if( in == NULL || in[0] == '\0' )
    {
        snprintf( out, len, "unknown" );
        return;
    }

    for( i = 0; in[i] != '\0' && i < len - 1; i++ )
    {
        unsigned char ch = (unsigned char) in[i];
        out[i] = ( isalnum( ch ) || ch == '.' || ch == '-' ) ? (char) ch : '_';
    }
    out[i] = '\0';
}
