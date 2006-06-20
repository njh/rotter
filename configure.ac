
dnl Require autoconf version >= 2.57
AC_PREREQ(2.57)


dnl ############# Initialization

AC_INIT([rotter], [0.1], [njh@ecs.soton.ac.uk])

AC_CONFIG_SRCDIR( src/rotter.c )
AC_CANONICAL_SYSTEM

dnl Version 1.7 of automake is recommended
AM_INIT_AUTOMAKE( 1.7 )
AM_CONFIG_HEADER( src/config.h )



dnl ############# Compiler and tools Checks

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_C_INLINE



dnl ############## Library Checks

AC_CHECK_LIB([m], [sqrt], , [AC_MSG_ERROR(Can't find libm)])
AC_CHECK_LIB([mx], [powf])

# Check for JACK (need 0.100.0 for jack_client_open)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)

# Check for TwoLAME
PKG_CHECK_MODULES(TWOLAME, twolame >= 0.3.7)

# Check for LAME
AC_CHECK_LIB(mp3lame, lame_get_version, 
	[ AC_CHECK_HEADER( lame/lame.h,
		[ LAME_CFLAGS="" 
		  LAME_LIBS="-lmp3lame" ],
		[ AC_MSG_ERROR(Found libmp3lame but can't find <lame/lame.h>)]
	  ) 
	],	[ AC_MSG_ERROR(Can't find libmp3lame, you need to have LAME installed)]
)
AC_SUBST( LAME_CFLAGS )
AC_SUBST( LAME_LIBS )


dnl ############## Header Checks

AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_CHECK_HEADERS( [termios.h] )


dnl ############## Function Checks
AC_CHECK_FUNCS( atexit usleep )



dnl ############## Final Output

AC_OUTPUT([Makefile src/Makefile])

