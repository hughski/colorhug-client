SUBDIRS = 						\
	icons

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = colorhug-completion.bash

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-ccmx.desktop.in			\
	colorhug-flash.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@GSETTINGS_RULES@
@INTLTOOL_XML_NOMERGE_RULE@
gsettings_schemas_in_files = com.hughski.colorhug-client.gschema.xml.in
gsettings_SCHEMAS = $(gsettings_schemas_in_files:.gschema.xml.in=.gschema.xml)

pkgdata_DATA =						\
	ch-ccmx.ui					\
	ch-flash.ui

EXTRA_DIST =						\
	$(gsettings_schemas_in_files)			\
	$(desktop_in_files)				\
	$(pkgdata_DATA)

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	com.hughski.colorhug-client.gschema.xml		\
	colorhug-ccmx.desktop				\
	colorhug-flash.desktop

-include $(top_srcdir)/git.mk
