
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

AC_INIT([iot-app-framework], m4_esyscmd([build-aux/git-version-gen .tarball-version]))

AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([src/config.h])
AM_INIT_AUTOMAKE([-Wno-portability subdir-objects foreign])

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

m4_define(version_major, `echo $VERSION | cut -d. -f1 | cut -d- -f1`)
m4_define(version_minor, `echo $VERSION | cut -d. -f2 | cut -d- -f1`)
m4_define(version_patch, `echo $VERSION | cut -d. -f3 | cut -d- -f1`)

AC_SUBST(VERSION)
AC_SUBST(VERSION_MAJOR, version_major)
AC_SUBST(VERSION_MINOR, version_minor)
AC_SUBST(VERSION_PATCH, version_patch)
AC_SUBST(VERSION_FULL, version_major.version_minor.version_patch)

IOT_VERSION_INFO="0:0:0"
AC_SUBST(IOT_VERSION_INFO)

# Disable static libraries.
AC_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
# We need AC_PROG_CXX if Qt support is enabled but (at least some
# versions of autotools) cannot handle conditional use of this.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_LIBTOOL

# Guesstimate native compiler if we're cross-compiling.
if test "$cross_compiling" != "no"; then
    AC_MSG_NOTICE([Looks like we're being cross-compiled...])
    if test -z "$CC_FOR_BUILD"; then
        CC_FOR_BUILD=cc
    fi
else
    AC_MSG_NOTICE([Looks like we're doing a native compilation...])
    CC_FOR_BUILD='$(CC)'
fi
AC_SUBST(CC_FOR_BUILD)
UNSHAVED_CC_FOR_BUILD="$CC_FOR_BUILD"

# Make first invocation of PKG_CHECK_MODULES 'if-then-else-fi'-safe.
PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_PATH_X
AC_CHECK_HEADERS([fcntl.h stddef.h stdint.h stdlib.h string.h sys/statvfs.h sys/vfs.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_HEADER_MAJOR
if test "$cross_compiling" = "no"; then
    AC_FUNC_MALLOC
fi
AC_FUNC_STRTOD
AC_CHECK_FUNCS([clock_gettime memmove memset regcomp strcasecmp strchr strdup strrchr strtol strtoul])

# Check and enable extra compiler warnings if they are supported.
AC_ARG_ENABLE(extra-warnings,
              [  --enable-extra-warnings enable extra compiler warnings],
	      [extra_warnings=$enableval], [extra_warnings=auto])

WARNING_CFLAGS=""
warncflags="-Wall -Wextra"
if test "$extra_warnings" != "no"; then
    save_CPPFLAGS="$CPPFLAGS"
    for opt in $warncflags; do
        AC_PREPROC_IFELSE([AC_LANG_PROGRAM([])],
	                  [WARNING_CFLAGS="$WARNING_CFLAGS $opt"])
    done
    CPPFLAGS="$save_CPPFLAGS"
fi

AC_SUBST(WARNING_CFLAGS)

# Check if debugging infra should be disabled.
AC_ARG_ENABLE(debug-infra,
              [  --enable-debug-infra    enable runtime debug messages],
              [enable_debug_infra=$enableval], [enable_debug_infra=yes])

if test "$enable_debug_infra" = "yes"; then
    AC_DEFINE([DEBUG_ENABLED], 1, [Enable runtime debug messages ?])
fi
AM_CONDITIONAL(DEBUG_ENABLED, [test "$enable_debug_infra" = "yes"])
AC_SUBST(DEBUG_ENABLED)

# Check if we should use PCRE for regexp matching.
AC_ARG_ENABLE(pcre,
              [  --enable-pcre           enable PCRE regexp backend],
	      [enable_pcre=$enableval], [enable_pcre=no])

if test "$enable_pcre" = "yes"; then
    PKG_CHECK_MODULES(PCRE, libpcre, [have_pcre=yes], [have_pcre=no])
    if test "$have_pcre" = "no"; then
        AC_MSG_ERROR([PCRE development libraries not found.])
    fi
    AC_MSG_NOTICE([Using PCRE regexp backend.])
else
    AC_MSG_NOTICE([Using POSIX/libc regexp backend.])
fi

if test "$enable_pcre" = "yes"; then
    AC_DEFINE([PCRE_ENABLED], 1, [Enable PCRE regexp backend ?])
    REGEXP_CFLAGS="$PCRE_CFLAGS"
    REGEXP_LIBS="$PCRE_LIBS"
else
    REGEXP_CFLAGS=""
    REGEXP_LIBS=""
fi
AM_CONDITIONAL(PCRE_ENABLED, [test "$enable_pcre" = "yes"])
AC_SUBST(PCRE_ENABLED)
AC_SUBST(PCRE_CFLAGS)
AC_SUBST(PCRE_LIBS)
AC_SUBST(REGEXP_CFLAGS)
AC_SUBST(REGEXP_LIBS)

# Check for json(-c).
PKG_CHECK_MODULES(JSON, [json], [have_json=yes], [have_json=no])

if test "$have_json" = "no"; then
    PKG_CHECK_MODULES(JSON, [json-c >= 0.11])
fi

AC_MSG_CHECKING([if json-c has headers under json-c include path])
saved_CFLAGS="$CFLAGS"
saved_LIBS="$LIBS"
CFLAGS="${JSON_CFLAGS}"
LIBS="${JSON_LIBS}"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
         [[#include <../json-c/json.h>]],
         [[return 0;]])],
    [json_include_jsonc=yes],
    [json_include_jsonc=no])
AC_MSG_RESULT([$json_include_jsonc])
CFLAGS="$saved_CFLAGS"
LIBS="$saved_LIBS"

if test "$json_include_jsonc" = "yes"; then
    AC_DEFINE([JSON_INCLUDE_PATH_JSONC], 1, [json headers under json-c ?])
fi

AC_MSG_CHECKING([for json_tokener_get_error()])
saved_CFLAGS="$CFLAGS"
saved_LIBS="$LIBS"
CFLAGS="${JSON_CFLAGS}"
LIBS="${JSON_LIBS}"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
         [[#include <json.h>]],
         [[json_tokener *tok = NULL;
           if (json_tokener_get_error(tok) != json_tokener_success)
              return 0;
           else
               return 1;]])],
    [have_json_tokener_get_error=yes],
    [have_json_tokener_get_error=no])
AC_MSG_RESULT([$have_json_tokener_get_error])
CFLAGS="$saved_CFLAGS"
LIBS="$saved_LIBS"

if test "$have_json_tokener_get_error" = "yes"; then
    AC_DEFINE([HAVE_JSON_TOKENER_GET_ERROR], 1, [json_tokener_get_error ?])
fi

# Allow substitution for LIBDIR and SYSCONFDIR.
AC_MSG_CHECKING([libdir])
AC_MSG_RESULT([$libdir])
AC_SUBST(LIBDIR, [$libdir])
AC_MSG_CHECKING([sysconfdir])
AC_MSG_RESULT([$sysconfdir])
AC_SUBST(SYSCONFDIR, [$sysconfdir])

# Shave by default.
SHAVE_INIT([build-aux], [enable])

# Generate output.
AC_CONFIG_FILES([
        build-aux/shave
        build-aux/shave-libtool
        Makefile
        src/Makefile
        src/common/iot-common.pc
        src/common/iot-pulse.pc
        src/common/iot-ecore.pc
        src/common/iot-glib.pc
        src/common/iot-uv.pc
        src/common/iot-qt.pc
    ])
AC_OUTPUT

# Display the configuration.
echo "----- configuration -----"
echo "Extra C warnings flags: $WARNING_CFLAGS"
echo "Debugging infra: $enable_debug_infra"
echo "Cross-compiling: $cross_compiling"
if test "$cross_compiling" != "no"; then
    echo "     * native compiler: $UNSHAVED_CC_FOR_BUILD"
fi
if test "$enable_pcre" = "yes"; then
    echo "Using PCRE regexp backend."
else
    echo "Using POSIX/libc regexp backend."
fi
