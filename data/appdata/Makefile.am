if HAVE_UNIX
@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files =						\
	com.hughski.ColorHug.CcmxLoader.appdata.xml.in		\
	com.hughski.ColorHug.DisplayAnalysis.appdata.xml.in	\
	com.hughski.ColorHug.FlashLoader.appdata.xml.in
endif

EXTRA_DIST = $(appdata_in_files)

CLEANFILES =							\
	com.hughski.ColorHug.CcmxLoader.appdata.xml		\
	com.hughski.ColorHug.DisplayAnalysis.appdata.xml	\
	com.hughski.ColorHug.FlashLoader.appdata.xml

-include $(top_srcdir)/git.mk
