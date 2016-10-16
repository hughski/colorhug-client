if HAVE_UNIX
@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files =						\
	com.hughski.ColorHug.Backlight.appdata.xml.in		\
	com.hughski.ColorHug.Spectro.appdata.xml.in		\
	com.hughski.ColorHug.CcmxLoader.appdata.xml.in		\
	com.hughski.ColorHug.DisplayAnalysis.appdata.xml.in
endif

EXTRA_DIST = $(appdata_in_files)

CLEANFILES =							\
	com.hughski.ColorHug.Backlight.appdata.xml		\
	com.hughski.ColorHug.Spectro.appdata.xml		\
	com.hughski.ColorHug.CcmxLoader.appdata.xml		\
	com.hughski.ColorHug.DisplayAnalysis.appdata.xml

-include $(top_srcdir)/git.mk
