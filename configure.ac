
dnl Require autoconf version >= 2.57
AC_PREREQ(2.57)


dnl ############# Initialization

AC_INIT([rotter], [0.8], [njh@aelius.com])

AC_CONFIG_SRCDIR([src/rotter.c])
AC_CANONICAL_SYSTEM

dnl Version 1.7 of automake is recommended
AM_INIT_AUTOMAKE([1.7])
AM_CONFIG_HEADER([src/config.h])



dnl ############# Compiler and tools Checks

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_C_INLINE



dnl ############## Library Checks

AC_CHECK_LIB([m], [sqrt], , [AC_MSG_ERROR(Can't find libm)])
AC_CHECK_LIB([m], [lrintf])
AC_CHECK_LIB([mx], [powf])

# Check for JACK (need 0.100.0 for jack_client_open)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)


# Check for TwoLAME
PKG_CHECK_MODULES(TWOLAME, twolame >= 0.3.9,
	[ HAVE_TWOLAME="Yes"
	  AC_DEFINE(HAVE_TWOLAME, 1, [TwoLAME library is available])
	], 
	[ HAVE_TWOLAME="No" 
	  AC_MSG_WARN(Can't find TwoLAME.)
	]
)


# Check for LAME
AC_CHECK_LIB(mp3lame, lame_get_version, 
	[ AC_CHECK_HEADER( lame/lame.h,
		[ HAVE_LAME="Yes"
	      AC_DEFINE(HAVE_LAME, 1, [LAME library is available])
		  LAME_CFLAGS="" 
		  LAME_LIBS="-lmp3lame" ],
		[ HAVE_LAME="No"
		  AC_MSG_WARN(Found libmp3lame but can't find <lame/lame.h>)]
	  ) 
	],	[ HAVE_LAME="No"
	      AC_MSG_WARN(Can't find libmp3lame)]
)


# Check for libsndfile
PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.14,
	[ HAVE_SNDFILE="Yes"
	  AC_DEFINE(HAVE_SNDFILE, 1, [SndFile library is available])
	], 
	[ HAVE_SNDFILE="No" 
	  AC_MSG_WARN(Can't find libsndfile.)
	]
)





dnl ############## Header Checks

AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_CHECK_HEADERS( [termios.h] )



dnl ############## Function Checks

AC_CHECK_FUNCS( usleep )



dnl ############## Compiler and Linker Flags

CFLAGS="$CFLAGS -Wunused -Wall $JACK_CFLAGS $TWOLAME_CFLAGS $LAME_CFLAGS $SNDFILE_CFLAGS"
LDFLAGS="$LDFLAGS $JACK_LIBS $TWOLAME_LIBS $LAME_LIBS $SNDFILE_LIBS"



dnl ############## Final Output

AC_OUTPUT([Makefile src/Makefile])


dnl ############## Summary

echo ""
echo "Building $PACKAGE_NAME version $PACKAGE_VERSION."
echo ""
echo "         TwoLAME codec (MP2): $HAVE_TWOLAME "
echo "            LAME codec (MP3): $HAVE_LAME "
echo "   libsndfile (WAV/AIFF etc): $HAVE_SNDFILE "
echo ""
echo "Next type 'make' to begin compilation."

