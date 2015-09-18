
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

# Check if development/developer mode should be disabled.
AC_ARG_ENABLE(development-mode,
              [  --enable-development-mode    enable development mode],
              [enable_devel_mode=$enableval], [enable_devel_mode=yes])

if test "$enable_devel_mode" = "yes"; then
    AC_DEFINE([DEVEL_MODE_ENABLED], 1, [Enable development mode ?])
fi
AM_CONDITIONAL(DEVEL_MODE_ENABLED, [test "$enable_devel_mode" = "yes"])
AC_SUBST(DEVEL_MODE_ENABLED)

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
              [  --enable-uv             enable LIBUV mainloop support],
	      [enable_libuv=$enableval], [enable_libuv=auto])

if test "$enable_libuv" != "no"; then
    PKG_CHECK_MODULES(LIBUV, libuv, [have_libuv=yes], [have_libuv=no])
    if test "$have_libuv" = "no" -a "$enable_libuv" = "yes"; then
        AC_MSG_ERROR([LIBUV development libraries not found.])
    fi

    enable_libuv="$have_libuv"
    if test "$have_libuv" = "yes"; then
        shared_libuv="yes"
    else
        shared_libuv="no"
    fi
else
    AC_MSG_NOTICE([LIBUV mainloop support is disabled.])
fi

# Check if NodeJS support was enabled.
CHECK_NODEJS()

echo "NODE_CXXFLAGS: $NODE_CXXFLAGS"
echo "NODE_LIBS: $NODE_LIBS"
echo "NODE_PREFIX: $NODE_PREFIX"
echo "LIBUV_CFLAGS: $LIBUV_CFLAGS"
echo "LIBUV_LIBS: $LIBUV_LIBS"
echo "LIBUV_SHARED: $LIBUV_SHARED"

# Check for python
AC_ARG_ENABLE(python,
              [ --enable-python   enable Python bindings],
        [enable_python=$enableval], [enable_python=no])

if test "$enable_python" = "yes"; then
    PKG_CHECK_MODULES(PYTHON, python,
                      [have_python=yes], [have_python=no])
    if test "$have_python" = "no"; then
        AC_MSG_ERROR([Python development libraries not found.])
    fi

    if test "$enable_glib" != "yes"; then
        PKG_CHECK_MODULES(GLIB, glib-2.0,
                        [have_glib=yes], [have_glib=no])
        if test "$have_glib" = "no" -a "$enable_glib" = "yes"; then
            AC_MSG_ERROR([Python support requires glib.])
        fi
    fi

    GLIB_INCLUDES_TEMP=$(echo $GLIB_CFLAGS | tr -s "\t" | tr " " "\n" | \
        grep "^-I" | sed "s/-I//g")
    t=""
    GLIB_INCLUDES=""
    for i in $GLIB_INCLUDES_TEMP;
    do
        GLIB_INCLUDES="$GLIB_INCLUDES$t\"$i\""
        t=","
    done
fi

AC_SUBST(GLIB_INCLUDES)
AM_CONDITIONAL(PYTHON_ENABLED, [test "$enable_python" = "yes"])

# Python build arguments for bitbake
PYTHON_CONFIG([PYTHONPATH])
PYTHON_CONFIG([BUILD_SYS])
PYTHON_CONFIG([HOST_SYS])
PYTHON_CONFIG([STAGING_INCDIR])
PYTHON_CONFIG([STAGING_LIBDIR])
PYTHON_CONFIG([INSTALL_LIB])
PYTHON_CONFIG([INSTALL_DATA])

PYTHON_EXECUTABLE=python
PYTHON_CONFIG([PYTHON_EXECUTABLE])

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

# Check if systemd socket-based activation was enabled.
AC_ARG_ENABLE(systemd,
              [  --enable-systemd       enable systemd socket-based activation],
	      [enable_systemd=$enableval], [enable_systemd=auto])

if test "$enable_systemd" != "no"; then
    PKG_CHECK_MODULES(SYSTEMD, libsystemd-daemon,
                      [have_systemd=yes], [have_systemd=no])
    if test "$have_systemd" = "no"; then
        PKG_CHECK_MODULES(SYSTEMD, libsystemd,
                      [have_systemd=yes], [have_systemd=no])
    fi
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

# Check for libcap.
PKG_CHECK_MODULES(LIBCAP, [libcap], [have_libcap=yes], [have_libcap=no])

# Older libcap did not have pkg-config file. Try to deal with those.
if test "$have_libcap" = "no"; then
    AC_CHECK_HEADERS([sys/capability.h], [have_libcap=yes], [have_libcap=no])
    AC_CHECK_LIB([cap], [cap_init], [have_libcap=yes], [have_libcap=no])
fi

if test "$have_libcap" = "no"; then
    AC_MSG_ERROR([Required libcap library not found.])
else
    LIBCAP_CFLAGS=""
    LIBCAP_LIBS="-lcap"
fi

AC_SUBST(LIBCAP_CFLAGS)
AC_SUBST(LIBCAP_LIBS)

# Check if support for Security Framework is enabled.
AC_ARG_ENABLE(security,
              [  --enable-security        enable support for Security Framework],
	      [enable_security=$enableval], [enable_security=yes])

if test "$enable_security" = "yes"; then
    PKG_CHECK_MODULES(SECURITY, security-manager libsmack,
                      [have_security=yes], [have_security=no])
    if test "$have_security" = "no"; then
        AC_MSG_ERROR([security-manager development libraries not found.])
    fi
else
    AC_MSG_NOTICE([Security Framework support is disabled.])
fi

if test "$enable_security" = "yes"; then
    AC_DEFINE([ENABLE_SECURITY_MANAGER], 1, [Enable Security Framework support ?])
    AC_DEFINE([ENABLE_SMACK], 1, [Enable SMACK support ?])
fi
AM_CONDITIONAL(ENABLE_SECURITY_MANAGER, [test "$enable_security" = "yes"])
AM_CONDITIONAL(ENABLE_SMACK,            [test "$enable_security" = "yes"])

# Check for package manager backend
AC_ARG_WITH(backend,
            [  --with-backend           backend to use in package manager, eg. rpm4, rpm5],
            [backend=$with_backend], [backend=none])            
case $backend in
     rpm4)
        backend_type="rpm4"
        PKG_CHECK_MODULES(LIBRPM, [rpm < 5.0], [have_rpm=yes], [have_rpm=no])
        PKG_CHECK_MODULES(POPT, popt, [have_popt=yes], [have_popt=no])
        
        if test "$have_rpm" != "yes"; then
           AC_MSG_ERROR([RPM4 or popt development libraries not found.])
        fi
        
        AC_SUBST(LIBRPM_CFLAGS)
        AC_SUBST(POPT_CFLAGS)
        AC_SUBST(LIBRPM_LIBS)
        AC_SUBST(POPT_LIBS)
        ;;
     rpm5)
        backend_type="rpm5"
        PKG_CHECK_MODULES(LIBRPM, [rpm >= 5.0], [have_rpm=yes], [have_rpm=no])
        PKG_CHECK_MODULES(POPT, popt, [have_popt=yes], [have_popt=no])

        if test "$have_rpm" != "yes" -o "$have_popt" != "yes"; then
           AC_MSG_ERROR([RPM5 or popt development libraries not found.])
        fi

        AC_SUBST(LIBRPM_CFLAGS)
        AC_SUBST(POPT_CFLAGS)
        AC_SUBST(LIBRPM_LIBS)
        AC_SUBST(POPT_LIBS)
        ;;
      none|no)
        backend_type="none"
        ;;
     *)
        AC_MSG_ERROR([invalid or missing package manager backend])
        ;;
esac
AM_CONDITIONAL(BACKEND_RPM5, test "$backend_type" = "rpm5")
AM_CONDITIONAL(BACKEND_RPM4, test "$backend_type" = "rpm4")
AM_CONDITIONAL(BACKEND_DISABLED, test "$backend_type" = "none")
AC_SUBST(BACKEND_TYPE, $backend_type)

AC_ARG_WITH(package-prefix,
            [  --with-package-prefix    package prefixe eg. iot-appfw],
            [pkg_prefix="${with_package_prefix}-"], [pkg_prefix=""])
AC_SUBST(PKG_PREFIX, $pkg_prefix)

# test user
AC_SUBST(SAMPLE_USER, [mosse])
AM_CONDITIONAL(BUILD_SAMPLES, test "${SAMPLE_USER}x" != "x")


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

# Generate output.
AC_CONFIG_FILES([build-aux/shave
                 build-aux/shave-libtool
                 Makefile
                 utils/Makefile
                 src/Makefile
                 src/common/iot-common.pc
                 src/app/iot-app.pc
                 src/utils/iot-utils.pc
                 src/common/iot-pulse.pc
                 src/common/iot-ecore.pc
                 src/common/iot-glib.pc
                 src/common/iot-uv.pc
                 src/common/iot-qt.pc
                 src/nodejs/binding.gyp
                 src/python/setup.py
		 ])
AC_OUTPUT

# Display the configuration.
echo "----- configuration -----"
echo "Extra C warnings flags: $WARNING_CFLAGS"
echo "Debugging infra: $enable_debug_infra"
echo "Development mode: $enable_devel_mode"
echo "Cross-compiling: $cross_compiling"
if test "$cross_compiling" != "no"; then
    echo "     * native compiler: $UNSHAVED_CC_FOR_BUILD"
fi
echo "PulseAudio mainloop support: $enable_pulse"
echo "EFL/ecore mainloop support: $enable_ecore"
echo "glib mainloop support: $enable_glib"
echo "LIBUV mainloop support: $enable_libuv"
echo "    * libuv shared library: $shared_libuv"
if test "$LIBUV_SHARED" = "yes"; then
    echo "    * libuv CFLAGS: $LIBUV_CFLAGS"
    echo "    * libuv LIBS: $LIBUV_LIBS"
fi
echo "package manager backend: ${BACKEND_TYPE}"
echo "build sample packages: ${BUILD_SAMPLES}"
echo "NodeJS bindings: ${NODEJS_ENABLED:-no}"
if test "$NODEJS_ENABLED" = "yes"; then
    echo "    * NodeJS prefix: $NODE_PREFIX"
    echo "    * NodeJS path: $NODE_PREFIX/bin/node"
    echo "    * NodeJS CXXFLAGS: $NODE_CXXFLAGS"
    echo "    * NodeJS LIBS: $NODE_LIBS"
fi
echo "Python bindings: $enable_python"
echo "Qt mainloop support: $enable_qt"
if test "$enable_pcre" = "yes"; then
    echo "Using PCRE regexp backend."
else
    echo "Using POSIX/libc regexp backend."
fi
echo "Systemd socket-based activation: $enable_systemd"
