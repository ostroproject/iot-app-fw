
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

# Check if PulseAudio mainloop support was enabled.
AC_ARG_ENABLE(pulse,
              [  --enable-pulse          enable PulseAudio mainloop support],
	      [enable_pulse=$enableval], [enable_pulse=auto])

if test "$enable_pulse" != "no"; then
    PKG_CHECK_MODULES(PULSE, libpulse >= 0.9.22,
    			     [have_pulse=yes], [have_pulse=no])
    if test "$have_pulse" = "no" -a "$enable_pulse" = "yes"; then
        AC_MSG_ERROR([PulseAudio development libraries not found.])
    fi

    if test "$enable_gpl" = "no"; then
        if test "$enable_pulse" = "yes"; then
           AC_MSG_ERROR([PulseAudio support requires the --enable-gpl option.])
        else
           enable_pulse="no"
        fi
    else
        enable_pulse="$have_pulse"
    fi
else
    AC_MSG_NOTICE([PulseAudio mainloop support is disabled.])
fi

if test "$enable_pulse" = "yes"; then
    AC_DEFINE([PULSE_ENABLED], 1, [Enable PulseAudio mainloop support ?])
fi
AM_CONDITIONAL(PULSE_ENABLED, [test "$enable_pulse" = "yes"])
AC_SUBST(PULSE_ENABLED)
AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)

# Check if EFL/ecore mainloop support was enabled.
AC_ARG_ENABLE(ecore,
              [  --enable-ecore          enable EFL/ecore mainloop support],
	      [enable_ecore=$enableval], [enable_ecore=auto])


if test "$enable_ecore" != "no"; then
    # We are using features which are present only at ecore 1.2 onwards.
    PKG_CHECK_MODULES(ECORE, ecore >= 1.2,
                      [have_ecore=yes], [have_ecore=no])
    if test "$have_ecore" = "no" -a "$enable_ecore" = "yes"; then
        AC_MSG_ERROR([EFL/ecore development libraries not found.])
    fi

    enable_ecore="$have_ecore"
else
    AC_MSG_NOTICE([EFL/ecore mainloop support is disabled.])
fi

if test "$enable_ecore" = "yes"; then
    AC_DEFINE([ECORE_ENABLED], 1, [Enable EFL/ecore mainloop support ?])
fi
AM_CONDITIONAL(ECORE_ENABLED, [test "$enable_ecore" = "yes"])
AC_SUBST(ECORE_ENABLED)
AC_SUBST(ECORE_CFLAGS)
AC_SUBST(ECORE_LIBS)

# Check if glib mainloop support was enabled.
AC_ARG_ENABLE(glib,
              [  --enable-glib           enable glib mainloop support],
	      [enable_glib=$enableval], [enable_glib=auto])

if test "$enable_glib" != "no"; then
    PKG_CHECK_MODULES(GLIB, glib-2.0,
                      [have_glib=yes], [have_glib=no])
    if test "$have_glib" = "no" -a "$enable_glib" = "yes"; then
        AC_MSG_ERROR([glib development libraries not found.])
    fi

    enable_glib="$have_glib"
else
    AC_MSG_NOTICE([glib mainloop support is disabled.])
fi

if test "$enable_glib" = "yes"; then
    AC_DEFINE([GLIB_ENABLED], 1, [Enable glib mainloop support ?])
fi
AM_CONDITIONAL(GLIB_ENABLED, [test "$enable_glib" = "yes"])
AC_SUBST(GLIB_ENABLED)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

# Check if qt mainloop support was enabled.
AC_ARG_ENABLE(qt,
              [  --enable-qt             enable qt mainloop support],
              [enable_qt=$enableval], [enable_qt=auto])

if test "$enable_qt" != "no"; then
    PKG_CHECK_MODULES(QTCORE, QtCore,
                      [have_qt=yes], [have_qt=no])
    if test "$have_qt" = "no" -a "$enable_qt" = "yes"; then
        AC_MSG_ERROR([Qt(Core) development libraries not found.])
    fi

    enable_qt="$have_qt"
else
    AC_MSG_NOTICE([Qt mainloop support is disabled.])
fi

if test "$enable_qt" = "yes"; then
    AC_DEFINE([QT_ENABLED], 1, [Enable qt mainloop support ?])
    QT_MOC="`pkg-config --variable moc_location QtCore`"
    AC_SUBST(QT_MOC)
fi
AM_CONDITIONAL(QT_ENABLED, [test "$enable_qt" = "yes"])
AC_SUBST(QT_ENABLED)
AC_SUBST(QTCORE_CFLAGS)
AC_SUBST(QTCORE_LIBS)

# Check if UV mainloop support was enabled.
AC_ARG_ENABLE(uv,
              [  --enable-uv             enable UV mainloop support],
	      [enable_uv=$enableval], [enable_uv=auto])

if test "$enable_uv" != "no"; then
    PKG_CHECK_MODULES(UV, libuv,
                      [have_uv=yes], [have_uv=no])
    if test "$have_uv" = "no" -a "$enable_uv" = "yes"; then
        AC_MSG_ERROR([UV development libraries not found.])
    fi

    enable_uv="$have_uv"
else
    AC_MSG_NOTICE([UV mainloop support is disabled.])
fi

if test "$enable_uv" = "yes"; then
    AC_DEFINE([UV_ENABLED], 1, [Enable UV mainloop support ?])
fi

# Check if NodeJS support was enabled.
AC_ARG_ENABLE(nodejs,
              [  --enable-nodejs         enable NodeJS bindings],
	      [enable_nodejs=$enableval], [enable_nodejs=no])

if test "$enable_nodejs" = "yes"; then
    PKG_CHECK_MODULES(UV, libuv, [have_uv=yes], [have_uv=no])
    if test "$have_uv" = "no"; then
        AC_MSG_ERROR([UV development libraries (for NodeJS) not found.])
    fi

    enable_uv="$have_uv"
    
    AC_CHECK_PROG(have_npm, [npm], [yes], [no])
    if test "$have_npm" = "no"; then
        AC_MSG_ERROR([npm binary (for NodeJS) not found.])
    fi

    AC_CHECK_FILES([/usr/include/node/node.h
                    /usr/include/node/common.gypi
                    /usr/include/node/config.gypi],
                   [have_nodedev=yes], [have_nodedev=no])
    if test "$have_nodedev" = "no"; then
        AC_MSG_ERROR([NodeJS development files not found.])
    fi
else
    AC_MSG_NOTICE([NodeJS bindings disabled.])
fi

AM_CONDITIONAL(UV_ENABLED, [test "$enable_uv" = "yes"])
AC_SUBST(UV_ENABLED)
AC_SUBST(UV_CFLAGS)
AC_SUBST(UV_LIBS)

if test "$enable_nodejs" = "yes"; then
    AC_DEFINE([NODEJS_ENABLED], 1, [Enable NodeJS bindings ?])
fi
AM_CONDITIONAL(NODEJS_ENABLED, [test "$enable_nodejs" = "yes"])
AC_SUBST(NODEJS_ENABLED)

# Check if systemd socket-based activation was enabled.
AC_ARG_ENABLE(systemd,
              [  --enable-systemd       enable systemd socket-based activation],
	      [enable_systemd=$enableval], [enable_systemd=auto])

if test "$enable_systemd" != "no"; then
    PKG_CHECK_MODULES(SYSTEMD, libsystemd-daemon,
                      [have_systemd=yes], [have_systemd=no])
    if test "$have_systemd" = "no" -a "$enable_systemd" = "yes"; then
        AC_MSG_ERROR([Systemd development libraries not found.])
    fi

    enable_systemd="$have_systemd"
else
    AC_MSG_NOTICE([systemd support is disabled.])
fi

if test "$enable_systemd" = "yes"; then
    AC_DEFINE([SYSTEMD_ENABLED], 1, [Enable systemd socket-based activation ?])
fi

AM_CONDITIONAL(SYSTEMD_ENABLED, [test "$enable_systemd" = "yes"])
AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

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

# Set up IoT CFLAGS and LIBS.
IOT_CFLAGS=""
IOT_LIBS=""
AC_SUBST(IOT_CFLAGS)
AC_SUBST(IOT_LIBS)

# Allow substitution for LIBDIR and SYSCONFDIR.
AC_MSG_CHECKING([libdir])
AC_MSG_RESULT([$libdir])
AC_SUBST(LIBDIR, [$libdir])
AC_MSG_CHECKING([sysconfdir])
AC_MSG_RESULT([$sysconfdir])
AC_SUBST(SYSCONFDIR, [$sysconfdir])

# Shave by default.
SHAVE_INIT([build-aux], [enable])

# Create IoT symlink to src.
if test ! -L iot; then
    AC_MSG_NOTICE([Symlinking src to iot...])
    ln -s src iot
fi

# Generate output.
AC_CONFIG_FILES([build-aux/shave
                 build-aux/shave-libtool
                 Makefile
                 utils/Makefile
                 src/Makefile
                 src/common/iot-common.pc
                 src/app/iot-app.pc
                 src/common/iot-pulse.pc
                 src/common/iot-ecore.pc
                 src/common/iot-glib.pc
                 src/common/iot-uv.pc
                 src/common/iot-qt.pc
                 packaging.in/iot-launch.service
                 packaging.in/iot-launch.socket
		 ])
AC_OUTPUT

# Display the configuration.
echo "----- configuration -----"
echo "Extra C warnings flags: $WARNING_CFLAGS"
echo "Cross-compiling: $cross_compiling"
if test "$cross_compiling" != "no"; then
    echo "     * native compiler: $UNSHAVED_CC_FOR_BUILD"
fi
echo "PulseAudio mainloop support: $enable_pulse"
echo "EFL/ecore mainloop support: $enable_ecore"
echo "glib mainloop support: $enable_glib"
echo "UV mainloop support: $enable_uv"
echo "NodeJS bindings: $enable_nodejs"
echo "Qt mainloop support: $enable_qt"
echo "Systemd socket-based activation: $enable_systemd"
