SUBDIRS = 						\
	icons

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-flash.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

pkgdata_DATA =						\
	ch-flash.ui

EXTRA_DIST =						\
	$(desktop_in_files)				\
	$(pkgdata_DATA)

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	colorhug-flash.desktop

-include $(top_srcdir)/git.mk
