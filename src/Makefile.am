AM_CFLAGS =
AM_CXXFLAGS =
AM_CPPFLAGS =
AM_LDFLAGS =

bin_PROGRAMS = nwipe

# Versioning; we use FORCE to always re-build this header.
BUILT_SOURCES = git_hash.h nwipe_logo.h
CLEANFILES = git_hash.h nwipe_logo.h

git_hash.h: FORCE
	@h="$${NWIPE_GIT_HASH-}"; \
	test -n "$$h" || h=`test -d $(top_srcdir)/.git && cd $(top_srcdir) && git describe --always --dirty 2>/dev/null || echo unknown`; \
	printf '#define NWIPE_GIT_HASH "%s"\n' "$$h" > $@

FORCE:

# FORK: company logo for the compact PDF report, chosen at build time with
# ./configure --with-logo=FILE. The file is embedded as a byte array; without
# --with-logo the header only defines nothing and the report has no logo.
# Depends on Makefile so re-running configure with another logo rebuilds it.
nwipe_logo.h: Makefile $(NWIPE_LOGO_FILE)
	$(AM_V_GEN)f='$(NWIPE_LOGO_FILE)'; \
	{ echo '/* Generated at build time from ./configure --with-logo. Do not edit. */'; \
	  if test -n "$$f"; then \
	    echo '#define NWIPE_HAVE_LOGO 1'; \
	    echo 'static const unsigned char nwipe_logo_data[] = {'; \
	    od -An -v -tx1 "$$f" | sed -e 's/[0-9a-f][0-9a-f]/0x&,/g' || exit 1; \
	    echo '};'; \
	  fi; } > $@.tmp && mv $@.tmp $@

# Core sources
NWIPE_CORE = \
    context.h \
    logging.c logging.h \
    options.c options.h \
    prng.c prng.h \
    version.c version.h \
    temperature.c temperature.h \
    nwipe.c nwipe.h \
    gui.c gui.h \
    method.c method.h \
    pass.c pass.h \
    pass_internal.c pass_internal.h \
    pass_static.c pass_random.c pass_scatter.c \
    device.c device.h \
    se_ata.c se_ata.h se_ata_gui.c se_ata_gui.h \
    se_nvme.c se_nvme.h se_nvme_gui.c se_nvme_gui.h \
    hpa_dco.c hpa_dco.h \
    miscellaneous.c miscellaneous.h \
    conf.c conf.h \
    customers.c customers.h \
    cpu_features.c cpu_features.h \
    round_size.c round_size.h \
    ANSI-color-codes.h \
	display_help.c \
	host_identity.c host_identity.h

# PRNG implementations
NWIPE_PRNG = \
    isaac_rand/isaac_standard.h \
    isaac_rand/isaac_rand.c isaac_rand/isaac_rand.h \
    isaac_rand/isaac64.c isaac_rand/isaac64.h \
    mt19937ar-cok/mt19937ar-cok.c mt19937ar-cok/mt19937ar-cok.h \
    alfg/add_lagg_fibonacci_prng.c alfg/add_lagg_fibonacci_prng.h \
    xor/xoroshiro256_prng.c xor/xoroshiro256_prng.h \
    splitmix64/splitmix64.c splitmix64/splitmix64.h \
    aes/aes_ctr_prng.cpp aes/aes_ctr_prng.h \
    chacha20/chacha20.c chacha20/chacha20.h \
    chacha20/chacha20_test.c chacha20/chacha20_test_vectors.c chacha20/chacha20_test.h

# PDF generation and reporting
NWIPE_PDF = \
    PDFGen/pdfgen.c PDFGen/pdfgen.h \
    create_pdf.c create_pdf.h \
    create_pdf_graph.c \
    create_single_disc_pdf.c \
	create_system_multi_disc_pdf.c \
	create_compact_pdf.c create_compact_pdf.h \
    embedded_images/shred_db.jpg.c embedded_images/shred_db.jpg.h \
    embedded_images/tick_erased.jpg.c embedded_images/tick_erased.jpg.h \
    embedded_images/redcross.c embedded_images/redcross.h \
    embedded_images/nwipe_exclamation.jpg.c embedded_images/nwipe_exclamation.jpg.h

# HDD temperature (SCSI)
NWIPE_HDDTEMP = \
    hddtemp_scsi/hddtemp.h hddtemp_scsi/scsi.h hddtemp_scsi/scsicmds.h \
    hddtemp_scsi/get_scsi_temp.c hddtemp_scsi/scsi.c hddtemp_scsi/scsicmds.c

# Combine all sources
nwipe_SOURCES = $(NWIPE_CORE) $(NWIPE_PRNG) $(NWIPE_PDF) $(NWIPE_HDDTEMP)
nwipe_CPPFLAGS = $(AM_CPPFLAGS) $(NCURSES_CFLAGS) $(PANEL_CFLAGS) \
                 $(LIBCONFIG_CFLAGS) $(PARTED_CFLAGS) $(LIBNVME_CFLAGS)
nwipe_LDADD    = $(PANEL_LIBS) $(NCURSES_LIBS) $(LIBCONFIG_LIBS) \
                 $(PARTED_LIBS) $(PARTED_STATIC_LIBS) $(LIBNVME_LIBS)

# Combine the test suite
check_PROGRAMS = test_round_size
TESTS = test_round_size
test_round_size_SOURCES = $(top_srcdir)/tests/unit/test_round_size.c round_size.c round_size.h
test_round_size_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src -I$(top_builddir)
