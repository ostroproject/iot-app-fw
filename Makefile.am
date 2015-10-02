SUBDIRS  = . utils src
doc_DATA = AUTHORS ChangeLog NEWS README

# This is the only way with automake I know of to force 'check-git-hooks'
# to be evaluated before 'all'. If there is a nicer way, I'm all ears...
BUILT_SOURCES = install-git-hooks install-iot-symlink


# command to copy a file while substituting libexecdir,bindir, and prefix
substcp = sed                                     \
            -e 's|@libexecdir[@]|$(libexecdir)|g' \
            -e 's|@bindir[@]|$(bindir)|g'         \
            -e 's|@prefix[@]|$(prefix)|g'

###################################
# git hook management
#

install-git-hooks:
	if test -d githooks -a -d .git; then                        \
	    for hook in githooks/???* kludge~; do                   \
	        case $$hook in                                      \
	            *.sample|*~|*.swp) continue;;                   \
	        esac;                                               \
		echo "Testing hook $$hook...";                      \
	        if test -x $$hook -a                                \
		      ! -x .git/hooks/$${hook##*/}; then            \
	            echo "Installing git hook $${hook##*/}...";     \
	            cp $$hook .git/hooks;                           \
	            chmod a+x .git/hooks/$${hook##*/};              \
	        fi                                                  \
	    done                                                    \
	fi

install-iot-symlink:
	if test ! -L $(top_srcdir)/iot; then		\
	   ln -sf ./src $(top_srcdir)/iot;		\
	fi;						\
	if test ! -L $(top_builddir)/iot; then		\
	   ln -sf ./src $(top_builddir)/iot;		\
	fi

install-data-hook:
	mkdir -p $(DESTDIR)/lib/systemd/system
if SYSTEMD_ENABLED
	$(substcp) $(top_srcdir)/packaging.in/iot-launch.service-socket.in  \
	    > $(DESTDIR)/lib/systemd/system/iot-launch.service
	$(substcp) $(top_srcdir)/packaging.in/iot-launch.socket.in	      \
	    > $(DESTDIR)/lib/systemd/system/iot-launch.socket
	$(substcp) $(top_srcdir)/packaging.in/iotpm-register-apps.service.in  \
	    > $(DESTDIR)/lib/systemd/system/iotpm-register-apps.service
else
	$(substcp) $(top_srcdir)/packaging.in/iot-launch.service.in	      \
	    > $(DESTDIR)/lib/systemd/system/iot-launch.service
endif

# generate documentation
generate-docs: Doxyfile
	doxygen $<

# cleanup
clean-local:
	rm -f *~
