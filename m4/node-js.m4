AC_DEFUN([CHECK_NODEJS],
[

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
    if test "$node" = "yes"; then
        node="node"
    fi
    AC_MSG_NOTICE([Looking for nodejs .pc file $node])
    PKG_CHECK_MODULES(NODE, $node, [have_node=yes], [have_node=no])
    if test "$have_node" = "no"; then
        AC_MSG_ERROR([NodeJS development files not found.])
    fi

    NODE_PREFIX="$(pkg-config --variable=variables_node_prefix $node)"
    NODE_CXXFLAGS="$NODE_CFLAGS -I$NODE_PREFIX/include"
    NODE_CXXFLAGS="$NODE_CXXFLAGS -I$NODE_PREFIX/include/node"
    shared_libuv="$(pkg-config --variable=variables_node_shared_libuv $node)"
    
    if test "$shared_libuv" = "true"; then
        PKG_CHECK_MODULES(UV, libuv, [have_libuv=yes], [have_libuv=no])

        if test "$have_libuv" = "no"; then
            AC_MSG_ERROR([Shared libuv for NodeJS not found])
        fi
        
        shared_libuv="yes"
    else
        shared_libuv="no"
        LIBUV_CFLAGS="$NODE_CXXFLAGS"
        LIBUV_LIBS=""
    fi

    LIBUV_ENABLED=yes
    enable_libuv=yes
    enable_node=yes
else
    AC_MSG_NOTICE([NodeJS bindings disabled.])
fi

if test "$enable_libuv" = "yes"; then
    LIBUV_ENABLED=yes
    AC_DEFINE([LIBUV_ENABLED], 1, [Enable UV mainloop support ?])
fi
if test "$shared_libuv" = "yes"; then
    LIBUV_SHARED=yes
    AC_DEFINE([LIBUV_SHARED], 1, [Shared UV library available ?])
fi

AM_CONDITIONAL(LIBUV_ENABLED, [test "$enable_libuv" = "yes"])
AM_CONDITIONAL(LIBUV_SHARED, [test "$shared_libuv" = "yes"])

AC_SUBST(LIBUV_ENABLED)
AC_SUBST(LIBUV_SHARED)
AC_SUBST(LIBUV_CFLAGS)
AC_SUBST(LIBUV_LIBS)

if test "$enable_node" = "yes"; then
    NODEJS_ENABLED=yes
    AC_DEFINE([NODEJS_ENABLED], 1, [Enable NodeJS bindings ?])
fi
AM_CONDITIONAL(NODEJS_ENABLED, [test "$enable_node" = "yes"])

AC_SUBST(NODEJS_ENABLED)
AC_SUBST(NODE_CXXFLAGS)
AC_SUBST(NODE_LIBS)
AC_SUBST(NODE_PREFIX)

AM_CONDITIONAL(WITH_NPM, [test "$with_npm" = "noooope"])
])
