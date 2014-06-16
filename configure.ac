AC_INIT([cplanet],[0.11],[https://fossil.etoilebsd.net/cplanet],[cplanet])
AC_CONFIG_SRCDIR([configure.ac])
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 foreign subdir-objects no-dist-gzip dist-xz])
AM_SILENT_RULES([yes])

PKG_CHECK_MODULES([SQLITE3],[sqlite3])
PKG_CHECK_MODULES([CURL],[libcurl])
PKG_CHECK_MODULES([EXPAT],[expat])

AC_CHECK_HEADER([ClearSilver.h], [
	AC_CHECK_LIB([neo_utl], [hdf_init], [], [AC_MSG_ERROR([libneo_utl is needed but not found])])
],[AC_MSG_ERROR([ClearSilver is needed but not found])])

AC_PROG_CC_STDC

AC_CONFIG_FILES([Makefile])
#AC_CONFIG_HEADERS(cplanet_config.h)
AC_OUTPUT
