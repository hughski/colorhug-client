SUBDIRS = 						\
	icons

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = colorhug-cmd-completion.bash

@INTLTOOL_DESKTOP_RULE@
pkgdata_in_files = colorhug-docs.desktop.in
pkgdata_DATA = $(pkgdata_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-spectro-util.desktop.in		\
	colorhug-ccmx.desktop.in			\
	colorhug-docs.desktop.in			\
	colorhug-flash.desktop.in			\
	colorhug-refresh.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@GSETTINGS_RULES@
gsettings_SCHEMAS = com.hughski.colorhug-client.gschema.xml

@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files =					\
	colorhug-ccmx.appdata.xml.in			\
	colorhug-flash.appdata.xml.in

UI_FILES =						\
	ch-spectro-util.ui				\
	ch-ccmx.ui					\
	ch-flash.ui					\
	ch-refresh.ui

EXTRA_DIST =						\
	$(gsettings_SCHEMAS)				\
	$(desktop_in_files)				\
	$(pkgdata_DATA)					\
	$(appdata_in_files)				\
	$(UI_FILES)

clean-local :
	rm -f *~

CLEANFILES =						\
	colorhug-ccmx.appdata.xml			\
	colorhug-flash.appdata.xml

DISTCLEANFILES =					\
	com.hughski.colorhug-client.gschema.xml		\
	colorhug-spectro-util.desktop			\
	colorhug-ccmx.desktop				\
	colorhug-docs.desktop				\
	colorhug-flash.desktop				\
	colorhug-refresh.desktop

-include $(top_srcdir)/git.mk
