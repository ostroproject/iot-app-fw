SUBDIRS          = . # common/tests
AM_CFLAGS        = $(WARNING_CFLAGS) $(AM_CPPFLAGS) $(PATHDEFS)
pkgconfigdir     = ${libdir}/pkgconfig

bin_PROGRAMS     =
sbin_PROGRAMS    =
bin_SCRIPTS      =
libexec_PROGRAMS =
noinst_PROGRAMS  =
lib_LTLIBRARIES  =
noinst_LIBRARIES =
pkgconfig_DATA   =
EXTRA_DIST       =

QUIET_GEN        = $(Q:@=@echo '  GEN   '$@;)

AM_CPPFLAGS      = -I$(top_builddir) -I$(top_builddir)/src \
                   -I$(top_srcdir) -I$(top_srcdir)/src
BUILT_SOURCES    =

GEN_LINKERSCRIPT = $(top_srcdir)/build-aux/gen-linker-script -b $(top_builddir)

PATHDEFS         = -DSYSCONFDIR=\"${sysconfdir}\" \
                   -DLIBDIR=\"${libdir}\" \
                   -DLIBEXECDIR=\"${libexecdir}\"


###################################
# common library
#

noinst_LIBRARIES += libcommon.a

libcommon_a_SOURCES =					\
		common/mm.c				\
		common/log.c				\
		common/debug.c				\
		common/json.c				\
		common/hash-table.c			\
		common/utils.c				\
		common/regexp.c				\
		common/file-utils.c

libcommon_a_CFLAGS =					\
		$(AM_CFLAGS)				\
		$(JSON_CFLAGS)				\
		$(REGEXP_CFLAGS)


###################################
# template library
#

noinst_LIBRARIES += libsmpl.a
noinst_PROGRAMS  += smpl-test

libsmpl_a_SOURCES =					\
		smpl/smpl.c				\
		smpl/errors.c				\
		smpl/symtbl.c				\
		smpl/parser.c				\
		smpl/preproc.c				\
		smpl/template-macro.c			\
		smpl/template-function.c		\
		smpl/template-block.c			\
		smpl/template-text.c			\
		smpl/template-vref.c			\
		smpl/template-branch.c			\
		smpl/template-loop.c			\
		smpl/template-switch.c			\
		smpl/builtin-functions.c		\
		smpl/buffer.c				\
		smpl/varref.c				\
		smpl/expr.c				\
		smpl/json.c				\
		smpl/addon.c				\
		smpl/result.c

libsmpl_a_CFLAGS =					\
		$(libcommon_a_CFLAGS)

libsmpl_a_LIBADD =					\
		libcommon.a


smpl_test_SOURCES =					\
		smpl/smpl-test.c

smpl_test_CFLAGS =					\
		$(libsmpl_a_CFLAGS)

smpl_test_LDADD =					\
		libsmpl.a				\
		libcommon.a				\
		$(JSON_LIBS)				\
		$(REGEXP_LIBS)				\
		-lm

###################################
# iot-service-generator
#

systemd_generator_PROGRAMS = iot-service-generator
systemd_generatordir = /lib/systemd/system-generators

iot_service_generator_PREPROCESSORS = \
		generators/service/preprocessors/whitelist-groups.c \
		generators/service/preprocessors/convert-manifest.c


iot_service_generator_SOURCES =				\
		generators/service/generator.c		\
		generators/service/config.c		\
		generators/service/application.c	\
		generators/service/self.c		\
		generators/service/scriptlet.c		\
		generators/service/manifest.c		\
		generators/service/service.c		\
		generators/service/template.c		\
		generators/service/filesystem.c		\
		$(iot_service_generator_PREPROCESSORS)

iot_service_generator_CFLAGS =		\
		$(AM_CFLAGS)		\
		$(PATHDEFS)		\
		$(SECURITY_CFLAGS)	\
		$(JSON_CFLAGS)		\
		$(REGEXP_CFLAGS)

iot_service_generator_LDADD =		\
		libsmpl.a		\
		libcommon.a		\
		$(JSON_LIBS)		\
		$(REGEXP_LIBS)		\
		-lm

SERVICE_TEMPLATES =       		\
		generators/service/templates/ostro/*.{template,inc}

SYSTEMD_UNITS = 	      		\
		generators/service/systemd/*.target

###################################
# iot-exec
#
#
#systemd_helper_PROGRAMS = iot-exec
#systemd_helperdir = /usr/libexec/iot
#
#iot_exec_SOURCES =				
#		generators/helpers/exec.c	
#		common/mm.c			
#		common/debug.c			
#		common/log.c			
#		common/hash-table.c		
#		common/utils.c
#
#iot_exec_CFLAGS =			
#		$(AM_CFLAGS)		
#		$(SECURITY_CFLAGS)
#
#iot_exec_LDADD =


# Install service file template(s).
install-service-templates:
	install -d $(DESTDIR)${libexecdir}/iot-app-fw/host && \
	install -d $(DESTDIR)${libexecdir}/iot-app-fw/self && \
	    cp -t $(DESTDIR)/${libexecdir}/iot-app-fw/host \
                $(top_srcdir)/data/templates/ostro/host/*.{template,inc} && \
	    cp -t $(DESTDIR)/${libexecdir}/iot-app-fw/self \
                $(top_srcdir)/data/templates/ostro/self/*.{template,inc}

# Install units for systemd.
install-systemd-units:
	mkdir -p $(DESTDIR)/lib/systemd/system
	    cp $(top_srcdir)/src/$(SYSTEMD_UNITS) \
	        $(DESTDIR)/lib/systemd/system

# Install various runtime data we need.
install-data-hook: install-service-templates install-systemd-units

# cleanup
clean-local:
	-rm -f *~
