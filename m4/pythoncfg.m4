AC_DEFUN([PYTHON_CONFIG],
[
    AC_ARG_WITH(python-$1,
                [  --with-python-$1           set $1 for Python],
                [PYTHON_$1=$with_python_$1], [$1=none])

    if test "$enable_python" = "no" -a "PYTHON_$1" != "none"; then
        AC_MSG_ERROR([Python support is not enabled.])
    fi
])

