SUBDIRS  = . src
doc_DATA = AUTHORS ChangeLog NEWS README

# This is the only way with automake I know of to force 'check-git-hooks'
# to be evaluated before 'all'. If there is a nicer way, I'm all ears...
BUILT_SOURCES = install-git-hooks install-iot-symlink

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
#	mkdir -p $(DESTDIR)/lib/systemd/system

# generate documentation
generate-docs: Doxyfile
	doxygen $<

# cleanup
clean-local:
	rm -f *~
