SUBDIRS          = . # common/tests
AM_CFLAGS        = $(WARNING_CFLAGS) $(AM_CPPFLAGS) \
		   -DSYSCONFDIR=\"@SYSCONFDIR@\" -DLIBDIR=\"@LIBDIR@\"
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
		generators/service/manifest-keys.c	\
		generators/service/container.c		\
		generators/service/container-nspawn.c	\
		generators/service/filesystem.c		\
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

# cleanup
clean-local:
	-rm -f *~
