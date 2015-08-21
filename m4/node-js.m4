AC_DEFUN([CHECK_NODEJS],
[
function node_config () {
    local _def="process.config.target_defaults"
    local _var="process.config.variables"
    local _v="[$]1"
    local _node=$node_path/bin/node

    if ! test -x $_node; then
        return 1
    fi

    case $_v in
        --cflags)
            $_node -e "var a = $_def.cflags, r = ''; for (i in a) \
                          r += ' ' + a[[i]]; \
                       var a = $_def.include_dirs; for (i in a) \
                          r += ' -I' + a[[i]]; \
                       var a = $_def.defines; for (i in a) \
                           r += ' ' + a[[i]]; \
                       console.log(r.trim());"
            ;;
        --libs)
            $_node -e "var a = ${_def}.libraries; r = ''; for (i in a) \
                          r += ' ' + a[[i]]; \
                       console.log(r.trim());"
            ;;
        *)
            $_node -e "var a = $_var.$_v; console.log(a);"
            ;;
    esac
}

AC_ARG_WITH(node,
            [  --with-node              enable NodeJS support],
            [node=$with_node], [node=none])

# Make the old --enable-nodejs be an alias for --with-node.
if test -z "$with_node"; then
    AC_ARG_ENABLE(nodejs,
                [  --enable-nodejs          enable default NodeJS support],
                [with_node=$enableval; node=$with_node], [node=none])
fi

if test -z "$with_node"; then
    with_node="no"
fi

if test "$with_node" != "no"; then
    enable_node=yes
    NODEJS_ENABLED=yes

    # See if we have or can find the NodeJS binary.
    if test -z "$node" -o "$node" = "yes"; then
        node=$(which node)
    fi

    node_path=${node%%/bin/node}

    if test -z "$node"; then
        AC_MSG_ERROR([NodeJS binary (node) neither given, nor found])
    fi

    if test ! -x "$node_path/bin/node"; then
        AC_MSG_ERROR([NodeJS binary ($node_path/bin/node) not executable.])
    fi

    # Check if we have the necessary NodeJS headers.
    saved_CXXFLAGS="$CXXFLAGS"
    _NODE_CXXFLAGS=$(node_config --cflags)
    CXXFLAGS="$CXXFLAGS $_NODE_CXXFLAGS"

    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([$node_path/include/node/node.h],
                     [have_nodeh=yes], [have_nodeh=no])
    AC_LANG_POP([C++])

    CXXFLAGS="$saved_CXXFLAGS"

    if test "$have_nodeh" = "no"; then
        AC_MSG_ERROR([NodeJS header ($node_path/include/node/node.h) not found.])
    fi

    _NODE_CXXFLAGS=$(node_config --cflags)
    _NODE_LIBS=$(node_config --libs)
    _NODE_PREFIX=$(node_config node_prefix)

    if test -n "$_NODE_PREFIX"; then
        _NODE_CXXFLAGS="$_NODE_CXXFLAGS -I$_NODE_PREFIX/include"
        _NODE_CXXFLAGS="$_NODE_CXXFLAGS -I$_NODE_PREFIX/include/node"
    fi

    if "$(node_config node_shared_libuv)" = "true"; then
        PKG_CHECK_MODULES(_UV, libuv, [have_uv=yes], [have_uv=no])

        if test "$have_uv" = "no"; then
            AC_MSG_ERROR([Shared libuv for NodeJS not found])
        fi

        _UV_STANDALONE=yes
    else
        _UV_STANDALONE=no
        _UV_CFLAGS="$_NODE_CXXFLAGS"
        _UV_LIBS=""
    fi

    _UV_ENABLED=yes
    if test -n "$1" -a "$1" != "_NODE"; then
        eval "$1_CXXFLAGS=\"$_NODE_CXXFLAGS\""
        eval "$1_LIBS=\"$_NODE_LIBS\""
        eval "$1_PREFIX=\"$_NODE_PREFIX\""
    fi
    if test -n "$2" -a "$2" != "_UV"; then
        eval "$2_CFLAGS=\"$_UV_CFLAGS\""
        eval "$2_LIBS=\"$_UV_LIBS\""
        eval "$2_ENABLED=\"$_UV_ENABLED\""
        eval "$2_STANDALONE=\"$_UV_STANDALONE\""
    fi
else
    AC_MSG_NOTICE([NodeJS bindings disabled.])
fi

AM_CONDITIONAL(UV_ENABLED, [test "$UV_ENABLED" = "yes"])
AM_CONDITIONAL(UV_STANDALONE, [test "$UV_STANDALONE" = "yes"])
AC_SUBST(UV_ENABLED)
AC_SUBST(UV_CFLAGS)
AC_SUBST(UV_LIBS)
if test "$UV_ENABLED" = "yes"; then
    AC_DEFINE([UV_ENABLED], 1, [Enable UV mainloop support ?])
fi
if test "$UV_STANDALONE" = "yes"; then
    AC_DEFINE([UV_STANDALONE], 1, [Standalone UV library available ?])
fi

AC_SUBST(NODE_CXXFLAGS)
AC_SUBST(NODE_LIBS)
AC_SUBST(NODE_PREFIX)
AM_CONDITIONAL(NODEJS_ENABLED, [test "$enable_node" = "yes"])
if test "$enable_node" = "yes"; then
    AC_DEFINE([NODEJS_ENABLED], 1, [Enable NodeJS bindings ?])
fi
AC_SUBST(NODEJS_ENABLED)

AM_CONDITIONAL(WITH_NPM, [test "$with_npm" = "noooope"])
])
