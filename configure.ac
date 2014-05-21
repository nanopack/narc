# rebuild: autoreconf -fvi 

AC_PREREQ(2.61)

AC_INIT([hookyd], [0.0.1])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/narc.c])

AC_PROG_CC

AC_CHECK_HEADERS(uv.h,
  [],
  [AC_MSG_ERROR([libuv header files not found.]); break]
)

AC_CHECK_LIB(uv, uv_run,
  [],
  [AC_MSG_ERROR([libuv libraries not found.])]
)

AC_OUTPUT(Makefile src/Makefile)
