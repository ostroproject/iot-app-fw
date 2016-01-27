SUBDIRS          = . # common/tests
AM_CFLAGS        = $(WARNING_CFLAGS) $(AM_CPPFLAGS) $(PATHDEFS)
IOT_CFLAGS       =
pkgconfigdir     = ${libdir}/pkgconfig

bin_PROGRAMS     =
sbin_PROGRAMS    =
bin_SCRIPTS      =
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

PATHDEFS         = -DSYSCONFDIR=\"${sysconfdir}\" \
                   -DLIBDIR=\"${libdir}\" \
                   -DLIBEXECDIR=\"${libexecdir}\"

###################################
# iot-service-generator
#

systemd_generator_PROGRAMS = iot-service-generator
systemd_generatordir = /lib/systemd/system-generators

iot_service_generator_SOURCES =				\
		generators/service/generator.c		\
		generators/service/config.c		\
		generators/service/application.c	\
		generators/service/manifest.c		\
		generators/service/service.c		\
		generators/service/template.c		\
		generators/service/filesystem.c		\
		jmpl/jmpl.c				\
		jmpl/scanner.c				\
		jmpl/parser.c				\
		jmpl/eval.c				\
		jmpl/symtab.c				\
		common/mm.c				\
		common/log.c				\
		common/debug.c				\
		common/hash-table.c			\
		common/utils.c				\
		common/file-utils.c			\
		common/regexp.c				\
		common/json.c

iot_service_generator_CFLAGS =		\
		$(AM_CFLAGS)		\
		$(PATHDEFS)		\
		$(SECURITY_CFLAGS)	\
		$(JSON_CFLAGS)		\
		$(REGEXP_CFLAGS)

iot_service_generator_LDADD =		\
		$(JSON_LIBS)		\
		$(REGEXP_LIBS)		\
		-lm

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


###################################
# JSON template test
#

noinst_PROGRAMS += jmpl-test

jmpl_test_SOURCES =					\
		jmpl/jmpl-test.c			\
		jmpl/jmpl.c				\
		jmpl/scanner.c				\
		jmpl/parser.c				\
		jmpl/eval.c				\
		jmpl/symtab.c				\
		common/mm.c				\
		common/log.c				\
		common/debug.c				\
		common/json.c				\
		common/hash-table.c			\
		common/utils.c				\
		common/regexp.c

jmpl_test_CFLAGS =			\
		$(AM_CFLAGS)		\
		$(JSON_CFLAGS)


jmpl_test_LDADD =			\
		$(JSON_LIBS)		\
		-lm


# Install service file template.
install-data-hook:
	mkdir -p $(DESTDIR)${libexecdir}/iot-app-fw && \
	    cp $(top_srcdir)/src/generators/service/service.jmpl \
	        $(DESTDIR)${libexecdir}/iot-app-fw

# cleanup
clean-local:
	-rm -f *~
