SUBDIRS          = . # common/tests
AM_CFLAGS        = $(WARNING_CFLAGS) $(AM_CPPFLAGS) \
		   -DSYSCONFDIR=\"@SYSCONFDIR@\" -DLIBDIR=\"@LIBDIR@\"
IOT_CFLAGS       =
pkgconfigdir     = ${libdir}/pkgconfig

bin_PROGRAMS     =
sbin_PROGRAMS    =
libexec_PROGRAMS =
noinst_PROGRAMS  =

lib_LTLIBRARIES  =
pkgconfig_DATA   =
EXTRA_DIST       =

QUIET_GEN        = $(Q:@=@echo '  GEN   '$@;)

AM_CPPFLAGS      = -I$(top_builddir) -I$(top_builddir)/src \
                   -I$(top_srcdir) -I$(top_srcdir)/src
BUILT_SOURCES    =

GEN_LINKERSCRIPT = $(top_srcdir)/build-aux/gen-linker-script -b $(top_builddir)

###################################
# IoT common library
#

lib_LTLIBRARIES += libiot-common.la
EXTRA_DIST      += common/iot-common.pc
pkgconfig_DATA  += common/iot-common.pc


libiot_commonh_ladir      =		\
		$(includedir)/iot

libiot_commonh_la_HEADERS =		\
		common.h		\
		config.h

libiot_common_ladir      =		\
		$(includedir)/iot/common

libiot_common_la_HEADERS =		\
		common/macros.h		\
		common/list.h		\
		common/log.h		\
		common/debug.h 		\
		common/env.h		\
		common/mm.h		\
		common/hash-table.h	\
		common/hashtbl.h	\
		common/mainloop.h	\
		common/utils.h		\
		common/socket-utils.h	\
		common/file-utils.h	\
		common/regexp.h		\
		common/refcnt.h		\
		common/fragbuf.h	\
		common/json.h		\
		common/transport.h	\
		common/mask.h

libiot_common_la_REGULAR_SOURCES =		\
		common/log.c			\
		common/debug.c			\
		common/env.c			\
		common/mm.c			\
		common/hash-table.c		\
		common/mainloop.c		\
		common/utils.c			\
		common/socket-utils.c		\
		common/file-utils.c		\
		common/regexp.c			\
		common/fragbuf.c		\
		common/json.c			\
		common/transport.c		\
		common/stream-transport.c	\
		common/dgram-transport.c

libiot_common_la_SOURCES =				\
		$(libiot_common_la_REGULAR_SOURCES)

libiot_common_la_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(JSON_CFLAGS)		\
		$(REGEXP_CFLAGS)

libiot_common_la_LDFLAGS =					\
		-Wl,-version-script=$(top_srcdir)/linker-script.common	\
		-version-info @IOT_VERSION_INFO@

libiot_common_la_LIBADD  = 		\
		$(JSON_LIBS)		\
		$(REGEXP_LIBS)		\
		-lrt

libiot_common_la_DEPENDENCIES =	\
		$(top_srcdir)/linker-script.common	\
		$(filter %.la, $(libiot_common_la_LIBADD))

# linker script generation
$(top_srcdir)/linker-script.common: $(libiot_common_la_HEADERS)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_common_la_CFLAGS)" \
		-p '^iot_|^_iot_|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.common


###################################
# IoT application library
#

lib_LTLIBRARIES += libiot-app.la
EXTRA_DIST      += app/iot-app.pc
pkgconfig_DATA  += app/iot-app.pc


libiot_apph_ladir      =				\
		$(includedir)/iot

libiot_apph_la_HEADERS =				\
		app.h

libiot_app_ladir      =					\
		$(includedir)/iot/app

libiot_app_la_HEADERS =					\
		app/app.h

libiot_app_la_REGULAR_SOURCES =				\
		app/app.c

libiot_app_la_SOURCES =					\
		$(libiot_app_la_REGULAR_SOURCES)

libiot_app_la_CFLAGS  =					\
		$(AM_CFLAGS)				\
		$(JSON_CFLAGS)

libiot_app_la_LDFLAGS =					\
		-Wl,-version-script=$(top_srcdir)/linker-script.app	\
		-version-info @IOT_VERSION_INFO@

libiot_app_la_LIBADD  = 				\
		$(JSON_LIBS)				\
		-lrt

libiot_app_la_DEPENDENCIES =	\
		$(top_srcdir)/linker-script.app	\
		$(filter %.la, $(libiot_app_la_LIBADD))

# linker script generation
$(top_srcdir)/linker-script.app: $(libiot_app_la_HEADERS)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_app_la_CFLAGS)" \
		-p '^iot_|^_iot_|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.app


###################################
# IoT pulse glue library
#

iot_pulse_headers =			\
		common/pulse-glue.h

if PULSE_ENABLED
lib_LTLIBRARIES += libiot-pulse.la
EXTRA_DIST      += common/iot-pulse.pc
pkgconfig_DATA  += common/iot-pulse.pc

libiot_pulse_ladir      =		\
		$(includedir)/iot/common

libiot_pulse_la_HEADERS = $(iot_pulse_headers)

libiot_pulse_la_SOURCES =		\
		common/pulse-glue.c

libiot_pulse_la_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(PULSE_CFLAGS)

libiot_pulse_la_LDFLAGS =		\
		-Wl,-version-script=$(top_srcdir)/linker-script.pulse \
		-version-info @IOT_VERSION_INFO@

libiot_pulse_la_LIBADD  =		\
		libiot-common.la	\
		$(PULSE_LIBS)

libiot_pulse_la_DEPENDENCIES =	\
		$(top_srcdir)/linker-script.pulse	\
		$(filter %.la, $(libiot_pulse_la_LIBADD))

libpulseincludedir      = $(includedir)/iot/pulse
libpulseinclude_HEADERS = $(libiot_pulse_la_HEADERS)
endif

# linker script generation
$(top_srcdir)/linker-script.pulse: $(iot_pulse_headers)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_pulse_la_CFLAGS)" \
                -p '^iot_|^_iot|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.pulse

###################################
# IoT ecore glue library
#

iot_ecore_headers =			\
		common/ecore-glue.h

if ECORE_ENABLED
lib_LTLIBRARIES += libiot-ecore.la
EXTRA_DIST      += common/iot-ecore.pc
pkgconfig_DATA  += common/iot-ecore.pc

libiot_ecore_ladir      =		\
		$(includedir)/iot/common

libiot_ecore_la_HEADERS = $(iot_ecore_headers)

libiot_ecore_la_SOURCES =		\
		common/ecore-glue.c

libiot_ecore_la_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(ECORE_CFLAGS)

libiot_ecore_la_LDFLAGS =		\
		-Wl,-version-script=$(top_srcdir)/linker-script.ecore \
		-version-info @IOT_VERSION_INFO@

libiot_ecore_la_LIBADD  =		\
		libiot-common.la	\
		$(ECORE_LIBS)

libiot_ecore_la_DEPENDENCIES =	\
		$(top_srcdir)/linker-script.ecore	\
		$(filter %.la, $(libiot_ecore_la_LIBADD))

libecoreincludedir      = $(includedir)/iot/ecore
libecoreinclude_HEADERS = $(libiot_ecore_la_HEADERS)
endif

# linker script generation
$(top_srcdir)/linker-script.ecore: $(iot_ecore_headers)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_ecore_la_CFLAGS)" \
		-p '^iot_|^_iot|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.ecore

###################################
# IoT glib glue library
#

iot_glib_headers =			\
		common/glib-glue.h

if GLIB_ENABLED
lib_LTLIBRARIES += libiot-glib.la
EXTRA_DIST      += common/iot-glib.pc
pkgconfig_DATA  += common/iot-glib.pc

libiot_glib_ladir      =		\
		$(includedir)/iot/common

libiot_glib_la_HEADERS = $(iot_glib_headers)

libiot_glib_la_SOURCES =		\
		common/glib-glue.c

libiot_glib_la_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(GLIB_CFLAGS)

libiot_glib_la_LDFLAGS =		\
		-Wl,-version-script=$(top_srcdir)/linker-script.glib \
		-version-info @IOT_VERSION_INFO@

libiot_glib_la_LIBADD  =		\
		libiot-common.la	\
		$(GLIB_LIBS)

libiot_glib_la_DEPENDENCIES =	\
		$(top_srcdir)/linker-script.glib	\
		$(filter %.la, $(libiot_glib_la_LIBADD))

libglibincludedir      = $(includedir)/iot/glib
libglibinclude_HEADERS = $(libiot_glib_la_HEADERS)
endif

# linker script generation
$(top_srcdir)/linker-script.glib: $(iot_glib_headers)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_glib_la_CFLAGS)" \
		-p '^iot_|^_iot|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.glib

###################################
# IoT UV glue library
#

iot_uv_headers =			\
		common/uv-glue.h

if UV_ENABLED
lib_LTLIBRARIES += libiot-uv.la
EXTRA_DIST      += common/iot-uv.pc
pkgconfig_DATA  += common/iot-uv.pc

libiot_uv_ladir      =		\
		$(includedir)/iot/common

libiot_uv_la_HEADERS = $(iot_uv_headers)

libiot_uv_la_SOURCES =		\
		common/uv-glue.c

libiot_uv_la_CFLAGS  =		\
		$(AM_CFLAGS)	\
		$(UV_CFLAGS)

libiot_uv_la_LDFLAGS =		\
		-Wl,-version-script=$(top_srcdir)/linker-script.uv \
		-version-info @IOT_VERSION_INFO@

libiot_uv_la_LIBADD  =		\
		libiot-common.la	\
		$(UV_LIBS)

libiot_uv_la_DEPENDENCIES =	\
		$(top_srcdir)/linker-script.uv	\
		$(filter %.la, $(libiot_uv_la_LIBADD))

libuvincludedir      = $(includedir)/iot/uv
libuvinclude_HEADERS = $(libiot_uv_la_HEADERS)
endif

# linker script generation
$(top_srcdir)/linker-script.uv: $(iot_uv_headers)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_uv_la_CFLAGS)" \
		-p '^iot_|^_iot|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.uv

###################################
# IoT Qt glue library
#

iot_qt_headers	=		\
		common/qt-glue.h

if QT_ENABLED
lib_LTLIBRARIES += libiot-qt.la
EXTRA_DIST      += common/iot-qt.pc
pkgconfig_DATA  += common/iot-qt.pc

BUILT_SOURCES   += common/qt-glue-priv.moc.h

libiot_qt_ladir      =		\
		$(includedir)/iot/common

libiot_qt_la_HEADERS = $(iot_qt_headers)

libiot_qt_la_SOURCES =		\
		common/qt-glue.cpp

libiot_qt_la_CPPFLAGS  =		\
		$(AM_CFLAGS)		\
		$(QTCORE_CFLAGS)

libiot_qt_la_LDFLAGS =		\
		-Wl,-version-script=$(top_srcdir)/linker-script.qt \
		-version-info @IOT_VERSION_INFO@

libiot_qt_la_LIBADD  = $(QTCORE_LIBS)

libiot_qt_la_DEPENDENCIES =		\
		$(top_srcdir)/linker-script.qt	\
		$(filter %.la, $(libiot_qt_la_LIBADD))

libqtincludedir      = $(includedir)/iot/qt
libqtinclude_HEADERS = $(libiot_qt_la_HEADERS)

# run moc to generate Qt meta objects
common/qt-glue-priv.moc.h: common/qt-glue-priv.h
	$(QUIET_GEN)$(QT_MOC) -DQT_ENABLED=1 $< -o$@
endif

# linker script generation
$(top_srcdir)/linker-script.qt: $(iot_qt_headers)
	$(QUIET_GEN)$(GEN_LINKERSCRIPT) -q \
		-P "$(CC)" -c "$(libiot_qt_la_CPPFLAGS) -DQT_ENABLED=1" \
		-p '^iot_|^_iot|^mrp_|^_mrp_' -o $@ $^

clean-linker-script::
	-rm -f $(top_srcdir)/linker-script.qt

###################################
# iot-launch-daemon
#

bin_PROGRAMS += iot-launch-daemon
EXTRA_DIST   += $(config_DATA)
configdir     = $(sysconfdir)/iot-launch
config_DATA   = 

iot_launch_daemon_SOURCES =				\
		launcher/daemon/daemon.c		\
		launcher/daemon/config.c		\
		launcher/daemon/signal.c		\
		launcher/daemon/transport.c		\
		launcher/daemon/cgroup.c		\
		launcher/daemon/application.c		\
		launcher/daemon/nftables.c		\
		launcher/daemon/security-manager.c	\
		launcher/daemon/event.c			\
		launcher/daemon/client.c		\
		launcher/daemon/message.c		\
		launcher/daemon/valgrind.c

iot_launch_daemon_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(BUILTIN_CFLAGS)	\
		$(JSON_CFLAGS)		\
		$(SYSTEMD_CFLAGS)	\
		-DLIBEXECDIR=\"@libexecdir@\"

iot_launch_daemon_LDADD   =		\
		libiot-common.la	\
		$(JSON_LIBS)		\
		$(SYSTEMD_LIBS)

iot_launch_daemon_LDFLAGS = 		\
		-rdynamic

install-data-hook::
#	rm -f $(DESTDIR)/$(sysconfdir)/iot-launch/??*
#	mkdir -p $(DESTDIR)/$(sysconfdir)/iot-launch

###################################
# iot-launch
#

bin_PROGRAMS += iot-launch

iot_launch_SOURCES =			\
		launcher/client/launch.c

iot_launch_CFLAGS  =			\
		$(AM_CFLAGS)

iot_launch_LDADD   =			\
		libiot-common.la

iot_launch_LDFLAGS =			\
		-rdynamic

agent-install-hook:
	$(MKDIR_P) $(DESTDIR)$(libexecdir)/iot-launcher &&	\
	ln -sf $(bindir)/iot-launch				\
		$(DESTDIR)$(libexecdir)/iot-launcher/iot-launch-agent

###################################
# iot-event-test
#

if GLIB_ENABLED
IOT_GLIB = libiot-glib.la
endif
if UV_STANDALONE
IOT_UV   = libiot-uv.la
endif

bin_PROGRAMS += iot-event-test

iot_event_test_SOURCES =		\
		app/tests/event-test.c

iot_event_test_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(GLIB_CFLAGS)		\
		$(UV_CFLAGS)

iot_event_test_LDADD   =		\
		libiot-app.la		\
		$(IOT_GLIB)		\
		$(IOT_UV)		\
		libiot-common.la

iot_event_test_LDFLAGS =		\
		-rdynamic

###################################
# iot-event-send
#

bin_PROGRAMS += iot-event-send

iot_event_send_SOURCES =		\
		app/tests/event-send.c

iot_event_send_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(GLIB_CFLAGS)		\
		$(UV_CFLAGS)

iot_event_send_LDADD   =		\
		libiot-app.la		\
		$(IOT_GLIB)		\
		$(IOT_UV)		\
		libiot-common.la

iot_event_send_LDFLAGS =		\
		-rdynamic

###################################
# iot-event-catch
#

bin_PROGRAMS += iot-event-catch

iot_event_catch_SOURCES =		\
		app/tests/event-catch.c

iot_event_catch_CFLAGS  =		\
		$(AM_CFLAGS)		\
		$(GLIB_CFLAGS)		\
		$(UV_CFLAGS)

iot_event_catch_LDADD   =		\
		libiot-app.la		\
		$(IOT_GLIB)		\
		$(IOT_UV)		\
		libiot-common.la

iot_event_catch_LDFLAGS =		\
		-rdynamic

###################################
# iot-app-list
#

bin_PROGRAMS += iot-app-list

iot_app_list_SOURCES =			\
		app/tests/list-test.c

iot_app_list_CFLAGS  =			\
		$(AM_CFLAGS)		\
		$(GLIB_CFLAGS)		\
		$(UV_CFLAGS)

iot_app_list_LDADD   =			\
		libiot-app.la		\
		$(IOT_GLIB)		\
		$(IOT_UV)		\
		libiot-common.la

iot_app_list_LDFLAGS =			\
		-rdynamic

###################################
# NodeJS bindings
#

if NODEJS_ENABLED
nodejs_LTLIBRARIES = libiot-nodejs.la
nodejsdir = $(NODE_PREFIX)/lib/node_modules/iot

libiot_nodejs_la_SOURCES = nodejs/src/node-appfw.cpp

libiot_nodejs_la_CXXFLAGS =		\
		$(AM_CFLAGS)		\
		$(NODE_CFLAGS)		\
		$(NODE_CXXFLAGS)

libiot_nodejs_la_LDFLAGS =

libiot_nodejs_la_LIBADD  = 		\
		libiot-app.la		\
		libiot-uv.la		\
		libiot-common.la	\
		$(NODE_LIBS)

nodejs-post-build-hook: libiot-nodejs.la
	cp $(top_builddir)/src/.libs/libiot-nodejs.so \
	    $(top_builddir)/src/.libs/iot-appfw.node

nodejs-install-hook:
	cp $(DESTDIR)/$(nodejsdir)/libiot-nodejs.so \
	    $(DESTDIR)/$(nodejsdir)/iot-appfw.node
	rm -f $(DESTDIR)/$(nodejsdir)/libiot-nodejs.so*
else
nodejs-post-build-hook:
nodejs-install-hook:
endif

###################################
# Python bindings
#

if PYTHON_ENABLED
python-bindings-build: libiot-glib.la libiot-common.la libiot-app.la
	if test ! -f python/python-app-fw-wrapper.cpp; then \
		d=$$(cd $(top_srcdir); pwd); \
		ln -sf $$d/src/python/python-app-fw-wrapper.cpp python; \
		ln -sf $$d/src/python/appfw.py python; \
	fi
	cd python && \
		STAGING_INCDIR=$(PYTHON_STAGING_INCDIR) \
		STAGING_LIBDIR=$(PYTHON_STAGING_LIBDIR) \
		BUILD_SYS=$(PYTHON_BUILD_SYS) \
		HOST_SYS=$(PYTHON_HOST_SYS) \
		$(PYTHON_PYTHON_EXECUTABLE) setup.py build ;

python-install-hook:
	cd python && \
	if test "x$(DESTDIR)" = "x"; then \
		PYTHONPATH=$(PYTHON_PYTHONPATH) \
		BUILD_SYS=$(PYTHON_BUILD_SYS) \
		HOST_SYS=$(PYTHON_HOST_SYS) \
		STAGING_INCDIR=$(PYTHON_STAGING_INCDIR) \
		STAGING_LIBDIR=$(PYTHON_STAGING_LIBDIR) \
		$(PYTHON_PYTHON_EXECUTABLE) \
		setup.py install \
		--install-lib=$(PYTHON_INSTALL_LIB) \
		--install-data=$(PYTHON_INSTALL_DATA); \
	else \
		PYTHONPATH=$(PYTHON_PYTHONPATH) \
		BUILD_SYS=$(PYTHON_BUILD_SYS) \
		HOST_SYS=$(PYTHON_HOST_SYS) \
		STAGING_INCDIR=$(PYTHON_STAGING_INCDIR) \
		STAGING_LIBDIR=$(PYTHON_STAGING_LIBDIR) \
		$(PYTHON_PYTHON_EXECUTABLE) \
		setup.py install \
		--install-lib=$(PYTHON_INSTALL_LIB) \
		--install-data=$(PYTHON_INSTALL_DATA) --root $(DESTDIR); \
	fi


python-bindings-clean:
	cd python && \
	if test -f setup.py; then \
	    BUILD_SYS=$(PYTHON_BUILD_SYS) \
	    HOST_SYS=$(PYTHON_HOST_SYS) \
	    STAGING_INCDIR=$(PYTHON_STAGING_INCDIR) \
	    STAGING_LIBDIR=$(PYTHON_STAGING_LIBDIR) \
	    $(PYTHON_PYTHON_EXECUTABLE) setup.py clean ; \
	else \
	    :; \
	fi
else
python-bindings-build:
python-install-hook:
python-bindings-clean:
endif

# Extra local actions to perform after a build.
all-local: nodejs-post-build-hook python-bindings-build

# Extra local eactions to perform after (binary) installation.
install-exec-hook: agent-install-hook nodejs-install-hook python-install-hook

# cleanup
clean-local: python-bindings-clean
	-rm -f *~
