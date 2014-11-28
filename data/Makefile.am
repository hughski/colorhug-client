SUBDIRS = 						\
	icons						\
	tests

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = colorhug-cmd-completion.bash

@INTLTOOL_DESKTOP_RULE@
pkgdata_in_files = colorhug-docs.desktop.in
pkgdata_DATA = $(pkgdata_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-docs.desktop.in			\
	com.hughski.ColorHug.CcmxLoader.desktop.in	\
	com.hughski.ColorHug.DisplayAnalysis.desktop.in	\
	com.hughski.ColorHug.FlashLoader.desktop.in	\
	com.hughski.ColorHug.SpectroUtil.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@GSETTINGS_RULES@
gsettings_SCHEMAS = com.hughski.colorhug-client.gschema.xml

@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files =					\
	com.hughski.ColorHug.CcmxLoader.appdata.xml.in	\
	com.hughski.ColorHug.FlashLoader.appdata.xml.in

UI_FILES =						\
	ch-spectro-util.ui				\
	ch-ccmx.ui					\
	ch-flash.ui					\
	ch-refresh-menus.ui				\
	ch-refresh.ui

SVG_FILES =						\
	colorhug-gray.svg				\
	usb.svg

RC_FILES =						\
	ch-ccmx.rc.in					\
	ch-flash.rc.in					\
	ch-main.rc.in					\
	ch-refresh.rc.in

EXTRA_DIST =						\
	$(gsettings_SCHEMAS)				\
	$(desktop_in_files)				\
	$(pkgdata_DATA)					\
	$(appdata_in_files)				\
	$(SVG_FILES)					\
	$(RC_FILES)					\
	$(UI_FILES)

clean-local :
	rm -f *~

CLEANFILES =						\
	com.hughski.ColorHug.CcmxLoader.appdata.xml	\
	com.hughski.ColorHug.FlashLoader.appdata.xml

DISTCLEANFILES =					\
	colorhug-docs.desktop				\
	com.hughski.ColorHug.CcmxLoader.desktop		\
	com.hughski.ColorHug.DisplayAnalysis.desktop	\
	com.hughski.ColorHug.FlashLoader.desktop	\
	com.hughski.ColorHug.SpectroUtil.desktop	\
	com.hughski.colorhug-client.gschema.xml.valid

-include $(top_srcdir)/git.mk
