# rebuild: autoreconf -fvi 

AC_PREREQ(2.61)

AC_INIT([narcd], [0.0.1])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/narc.c])

AC_PROG_CC

AC_CHECK_LIB(socket, ether_ntoa,
  [],
  [AC_MSG_ERROR([socket libraries not found.])]
)

AC_CHECK_LIB(pthread, pthread_mutexattr_init,
  [],
  [AC_MSG_ERROR([pthread libraries not found.])]
)

AC_CHECK_LIB(nsl, inet_aton,
  [],
  [AC_MSG_ERROR([nsl libraries not found.])]
)

AC_CHECK_LIB(kstat, kstat_data_lookup,
  [],
  [AC_MSG_ERROR([libkstat libraries not found.])]
)

AC_CHECK_LIB(sendfile, sendfile,
  [],
  [AC_MSG_ERROR([libsendfile libraries not found.])]
)

AC_CHECK_HEADERS(uv.h,
  [],
  [AC_MSG_ERROR([libuv header files not found.]); break]
)

AC_CHECK_LIB(uv, uv_run,
  [],
  [AC_MSG_ERROR([libuv libraries not found.])]
)

AC_OUTPUT(Makefile src/Makefile)
