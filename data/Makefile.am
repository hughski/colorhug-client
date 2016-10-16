SUBDIRS = 						\
	appdata						\
	icons						\
	tests

if HAVE_BASH_COMPLETION
SUBDIRS += 						\
	bash
endif

if HAVE_UNIX
@INTLTOOL_DESKTOP_RULE@
pkgdata_in_files = colorhug-docs.desktop.in
pkgdata_DATA = $(pkgdata_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-docs.desktop.in			\
	com.hughski.ColorHug.Backlight.desktop.in	\
	com.hughski.ColorHug.Spectro.desktop.in	\
	com.hughski.ColorHug.CcmxLoader.desktop.in	\
	com.hughski.ColorHug.DisplayAnalysis.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
endif

@GSETTINGS_RULES@
gsettings_SCHEMAS = com.hughski.colorhug-client.gschema.xml

UI_FILES =						\
	ch-backlight-menus.ui				\
	ch-backlight.ui					\
	ch-spectro-menus.ui				\
	ch-spectro.ui					\
	ch-ccmx.ui					\
	ch-ccmx-menus.ui				\
	ch-refresh-menus.ui				\
	ch-refresh.ui

SVG_FILES =						\
	colorhug-gray.svg				\
	usb-als.svg					\
	usb.svg

RC_FILES =						\
	ch-ccmx.rc.in					\
	ch-main.rc.in					\
	ch-refresh.rc.in

EXTRA_DIST =						\
	$(gsettings_SCHEMAS)				\
	$(desktop_in_files)				\
	$(pkgdata_DATA)					\
	$(SVG_FILES)					\
	$(RC_FILES)					\
	$(UI_FILES)

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	colorhug-docs.desktop				\
	com.hughski.ColorHug.Backlight.desktop		\
	com.hughski.ColorHug.Spectro.desktop		\
	com.hughski.ColorHug.CcmxLoader.desktop		\
	com.hughski.ColorHug.DisplayAnalysis.desktop	\
	com.hughski.colorhug-client.gschema.xml.valid

-include $(top_srcdir)/git.mk
