#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([nwipe],[0.43],[git@brumit.nl])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_FILES([Makefile src/Makefile man/Makefile])
AC_CONFIG_SRCDIR([src/nwipe.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
PKG_PROG_PKG_CONFIG

# Checks for libraries.
PKG_CHECK_MODULES([NCURSES],   [ncurses])
PKG_CHECK_MODULES([PANEL],     [panel])
PKG_CHECK_MODULES([LIBCONFIG], [libconfig])
PKG_CHECK_MODULES([PARTED],    [libparted])

AC_SEARCH_LIBS([pthread_create], [pthread], [], [AC_MSG_ERROR([pthread development library not found])])
AC_SEARCH_LIBS([clock_gettime], [rt])

# NVMe secure erase support.
AC_ARG_WITH([libnvme],
  [AS_HELP_STRING([--without-libnvme], [Build without NVMe secure erase support])],
  [], [with_libnvme=yes])
have_libnvme=no
AS_IF([test "x$with_libnvme" != xno],
  [PKG_CHECK_MODULES([LIBNVME], [libnvme >= 1.0],
    [
      AC_DEFINE([HAVE_LIBNVME], [1], [Define if libnvme is available])
      have_libnvme=yes
    ],
    [AC_MSG_ERROR([libnvme >= 1.0 not found; libnvme is required by default \
as most modern drives are NVMe. Install libnvme-devel (or libnvme-dev) to \
enable NVMe secure erase support, or pass --without-libnvme to build without it])])],
  [AC_MSG_WARN([building --without-libnvme; NVMe secure erase support disabled])])
AM_CONDITIONAL([HAVE_LIBNVME], [test "x$have_libnvme" = xyes])

have_sanact_verif=no
have_nvme_version_project=no
libnvme_version=
AS_IF([test "x$have_libnvme" = xyes], [
  save_CPPFLAGS=$CPPFLAGS
  CPPFLAGS="$CPPFLAGS $LIBNVME_CFLAGS"
  libnvme_version=$($PKG_CONFIG --modversion libnvme)
  AC_CHECK_DECL([NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF],
    [AC_DEFINE([HAVE_NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF], [1],
       [Define if libnvme has NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF])
     have_sanact_verif=yes],
    [],
    [[#include <libnvme.h>]])
  AC_CHECK_DECL([NVME_VERSION_PROJECT],
    [AC_DEFINE([HAVE_NVME_VERSION_PROJECT], [1],
       [Define if libnvme has NVME_VERSION_PROJECT])
     have_nvme_version_project=yes],
    [AC_DEFINE_UNQUOTED([NWIPE_LIBNVME_VERSION], ["$libnvme_version"],
       [libnvme version from pkg-config, used when NVME_VERSION_PROJECT is missing])],
    [[#include <libnvme.h>]])
  CPPFLAGS=$save_CPPFLAGS
])

# Libraries needed for static linking with parted.
AS_CASE([" $LDFLAGS "], [*" -static "*|*" -all-static "*], [
  save_LIBS=$LIBS; LIBS=
  AC_CHECK_LIB([uuid], [uuid_generate], [LIBS="-luuid $LIBS"],
    [AC_MSG_ERROR([libuuid is required for static linking but not found])])
  AC_CHECK_LIB([devmapper], [dm_task_create], [LIBS="-ldevmapper $LIBS"],
    [AC_MSG_ERROR([libdevmapper is required for static linking but not found])])
  AC_CHECK_LIB([blkid], [blkid_new_probe], [LIBS="-lblkid $LIBS"],
    [AC_MSG_ERROR([libblkid is required for static linking but not found])])
  PARTED_STATIC_LIBS=$LIBS; LIBS=$save_LIBS
])
AC_SUBST([PARTED_STATIC_LIBS])

# Checks for header files.
# FORK: optional company logo, embedded into the binary at build time and drawn
# in the compact PDF report. There is no runtime option on purpose.
AC_ARG_WITH([logo],
    [AS_HELP_STRING([--with-logo=FILE],
        [Embed a PNG or JPEG logo in the compact PDF report (default: no logo)])],
    [], [with_logo=no])
NWIPE_LOGO_FILE=
logo_summary=none
AS_CASE([$with_logo],
    [no|""], [],
    [yes], [AC_MSG_ERROR([--with-logo needs a file, e.g. --with-logo=/path/logo.png])],
    [*\ *], [AC_MSG_ERROR([logo path must not contain spaces: $with_logo])],
    [
    test -f "$with_logo" && test -r "$with_logo" || AC_MSG_ERROR([logo file not readable: $with_logo])
    AS_CASE([$with_logo], [/*], [NWIPE_LOGO_FILE=$with_logo], [NWIPE_LOGO_FILE=`pwd`/$with_logo])
    logo_magic=`od -An -N8 -tx1 "$NWIPE_LOGO_FILE" | tr -d ' \n'`
    AS_CASE([$logo_magic],
        [89504e470d0a1a0a], [
            logo_color=`od -An -j25 -N1 -tu1 "$NWIPE_LOGO_FILE" | tr -d ' \n'`
            logo_interlace=`od -An -j28 -N1 -tu1 "$NWIPE_LOGO_FILE" | tr -d ' \n'`
            AS_CASE([$logo_color], [4|6], [AC_MSG_ERROR([PNG logo has an alpha channel (transparency), which the PDF library cannot embed.
  Flatten it onto white first, e.g.:
    convert $with_logo -background white -alpha remove -alpha off logo-flat.png
  or save it as JPEG.])])
            test "x$logo_interlace" = x0 || AC_MSG_ERROR([interlaced PNG logo is not supported; re-save it without interlacing])
            logo_summary="PNG $NWIPE_LOGO_FILE"],
        [ffd8ff*], [logo_summary="JPEG $NWIPE_LOGO_FILE"],
        [AC_MSG_ERROR([logo must be a PNG or JPEG file: $with_logo])])
    ])
AC_SUBST([NWIPE_LOGO_FILE])

AC_CHECK_HEADERS([fcntl.h inttypes.h netinet/in.h stddef.h stdint.h stdlib.h \
                  string.h sys/file.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])

# Checks for library functions.
AC_CHECK_FUNCS([malloc fdatasync memset regcomp strdup strerror])

AC_OUTPUT

# Configuration summary.
AS_ECHO([""])
AS_ECHO(["  ---------------------------------------------"])
AS_ECHO(["   nwipe $VERSION"])
AS_ECHO(["  ---------------------------------------------"])
AS_ECHO(["    prefix ........................ $prefix"])
AS_ECHO(["    CC ............................ $CC"])
AS_ECHO(["    CXX ........................... $CXX"])
AS_ECHO(["    CFLAGS ........................ $CFLAGS"])
AS_ECHO(["    CXXFLAGS ...................... $CXXFLAGS"])
AS_ECHO(["    CPPFLAGS ...................... $CPPFLAGS"])
AS_ECHO(["    LDFLAGS ....................... $LDFLAGS"])
AS_ECHO([""])
AS_ECHO(["  NVMe secure erase ............. $have_libnvme"])
AS_ECHO(["  Report logo ................... $logo_summary"])
AS_IF([test "x$have_libnvme" = xyes], [
  AS_ECHO(["      libnvme version ............. $libnvme_version"])
  AS_ECHO(["      NVME_VERSION_PROJECT ........ $have_nvme_version_project"])
  AS_ECHO(["      SANACT_EXIT_MEDIA_VERIF ..... $have_sanact_verif"])
])
AS_ECHO(["  ---------------------------------------------"])
AS_ECHO([""])
