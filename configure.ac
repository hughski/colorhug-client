# Copyright (C) 2011-2014 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

AC_INIT([colorhug-client],[0.3.0],[http://www.hughski.com])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LIBTOOL

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AS_ALL_LINGUAS
AC_PROG_CC
AC_PROG_INSTALL
AC_OBJEXT
LT_INIT
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.50.0])
GLIB_GSETTINGS
GOBJECT_INTROSPECTION_CHECK([0.9.8])
YELP_HELP_INIT

dnl ---------------------------------------------------------------------------
dnl - gettext stuff
dnl ---------------------------------------------------------------------------
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE(GETTEXT_PACKAGE, "AC_PACKAGE_NAME", [foo])
AM_GLIB_GNU_GETTEXT

dnl ---------------------------------------------------------------------------
dnl - Use strict options (default enabled for devs, disabled in releases)
dnl ---------------------------------------------------------------------------
if test -d ".git"; then
	default_strict=yes
else
	default_strict=no
fi

AC_ARG_ENABLE(strict, AS_HELP_STRING([--enable-strict],
				     [Enable strict compilation options]),
				     enable_strict=$enableval,
				     enable_strict=$default_strict)
if test x$enable_strict != xno; then
	CPPFLAGS="$CPPFLAGS -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES"
	CPPFLAGS="$CPPFLAGS -DG_DISABLE_DEPRECATED"
	CPPFLAGS="$CPPFLAGS -DGSEAL_ENABLE"
fi

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------

if test "$GCC" = "yes"; then
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wall"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-declarations"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wpointer-arith"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wwrite-strings"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winit-self"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wreturn-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-nonliteral"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-security"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-include-dirs"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-format-attribute"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wclobbered"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wempty-body"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wignored-qualifiers"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wsign-compare"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wtype-limits"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wuninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Waggregate-return"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wdeclaration-after-statement"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wshadow"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wno-strict-aliasing"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winline"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-parameter-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Woverride-init"
else
	WARNINGFLAGS_C=""
fi
AC_SUBST(WARNINGFLAGS_C)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.44.0 gobject-2.0 gmodule-2.0 gio-2.0 >= 2.25.9)
PKG_CHECK_MODULES(GUSB, gusb >= 0.2.2)
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.11.2)
PKG_CHECK_MODULES(COLORD, colord >= 1.3.4)
PKG_CHECK_MODULES(COLORD_GTK, colord-gtk >= 0.1.24 lcms2)
PKG_CHECK_MODULES(SOUP, libsoup-2.4)
PKG_CHECK_MODULES(COLORHUG, colorhug >= 1.2.3)
AC_PATH_PROG(APPSTREAM_UTIL, [appstream-util], [unfound])

dnl ---------------------------------------------------------------------------
dnl - Sound effects using libcanberra are optional
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(CANBERRA, libcanberra-gtk3 >= 0.10,
		  have_canberra=yes, have_canberra=no)
if test x$have_canberra = xyes; then
	AC_DEFINE(HAVE_CANBERRA,1,[whether libcanberra-gtk3 is available])
fi

dnl ---------------------------------------------------------------------------
dnl - Check Linux-specific dependencies
dnl ---------------------------------------------------------------------------
AC_CHECK_DEFINE([__unix__], enable_unix="yes", enable_unix="no")
AM_CONDITIONAL(HAVE_UNIX, test x$enable_unix = xyes)

dnl ---------------------------------------------------------------------------
dnl - Make paths available for source files
dnl ---------------------------------------------------------------------------
AC_SUBST(SYSCONFDIR, $sysconfdir)

dnl ---------------------------------------------------------------------------
dnl - Is docbook2man available?
dnl ---------------------------------------------------------------------------
AC_PATH_PROG(DOCBOOK2MAN, docbook2man, no)
if test "$DOCBOOK2MAN" = "no" ; then
	AC_MSG_WARN([docbook2man not found, will not be able to build man documentation])
fi
AM_CONDITIONAL(HAVE_DOCBOOK2MAN, [test "$DOCBOOK2MAN" != "no"])

dnl ---------------------------------------------------------------------------
dnl - Is windres available?
dnl ---------------------------------------------------------------------------
AC_CHECK_TOOL([WINDRES], [windres], [no])
if test "$WINDRES" != "no" ; then
	AC_CHECK_TOOL([ICOTOOL], [icotool], [no])
else
	ICOTOOL=no
fi
AM_CONDITIONAL([HAS_WINDOWS_ICON], [test "$ICOTOOL" != "no"])

dnl ---------------------------------------------------------------------------
dnl - Bash completion
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(bash_completion, AS_HELP_STRING([--enable-bash-completion],[Enable bash completion]),
	      enable_bash_completion=$enableval, enable_bash_completion=yes)
if test x$enable_bash_completion != xno; then
	PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0])
	BASH_COMPLETIONS_DIR="`pkg-config --variable=completionsdir bash-completion`"
	AC_SUBST([BASH_COMPLETIONS_DIR])
fi
AM_CONDITIONAL([HAVE_BASH_COMPLETION],[test "x$enable_bash_completion" = "xyes"])

dnl ---------------------------------------------------------------------------
dnl - Build windows release build (no debugging window)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(release, AS_HELP_STRING([--enable-release],[Enable Windows release build]),
	      enable_release=$enableval,enable_release=no)
AM_CONDITIONAL(HAVE_WIN32_RELEASE, test x$enable_release = xyes)

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
data/ch-ccmx.rc
data/ch-main.rc
data/ch-refresh.rc
data/Makefile
data/appdata/Makefile
data/bash/Makefile
data/icons/Makefile
data/icons/16x16/Makefile
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/64x64/Makefile
data/icons/256x256/Makefile
data/icons/scalable/Makefile
data/tests/Makefile
help/Makefile
setup-win32.nsi
src/Makefile
man/Makefile
po/Makefile.in
])
AC_OUTPUT

dnl ==========================================================================
echo "
                    ColorHug $VERSION
                  ===================

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        cppflags:                  ${CPPFLAGS}
"
if test x$enable_unix = xno; then
	echo "
		windres:                   ${WINDRES}
		icotool:                   ${ICOTOOL}
	"
fi
