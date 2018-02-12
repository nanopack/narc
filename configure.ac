# rebuild: autoreconf -fvi 

AC_PREREQ(2.61)

AC_INIT([narcd], [0.2.2])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/narc.c])

AC_PROG_CC

AC_SEARCH_LIBS(ether_ntoa, socket,
  [],
  [AC_MSG_ERROR([socket libraries not found.])]
)

AC_SEARCH_LIBS(pthread_mutexattr_init, pthread,
  [],
  [AC_MSG_ERROR([pthread libraries not found.])]
)

AC_SEARCH_LIBS(inet_aton, nsl,
  [],
  [AC_MSG_ERROR([nsl libraries not found.])]
)

AC_CHECK_HEADERS(uv.h,
  [],
  [AC_MSG_ERROR([libuv header files not found.]); break]
)

AC_SEARCH_LIBS(uv_run, uv,
  []
  [AC_SEARCH_LIBS(uv_run, uv,
    [],
    [AC_MSG_ERROR([libuv libraries not found.])],
    -lsendfile -lkstat
  )]
)

AC_OUTPUT(Makefile src/Makefile)
