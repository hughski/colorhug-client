themedir = $(datadir)/icons/hicolor
size = scalable
context = apps

iconsdir = $(themedir)/$(size)/$(context)
icons_DATA = 		 		\
	colorhug.svg

appiconsdir = $(datadir)/colorhug-client/icons
appicons_DATA = 	 		\
	usb.svg	  			\
	colorhug-gray.svg

install-data-hook:
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/colorhug.svg $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes/application-x-ccmx.svg;

EXTRA_DIST =				\
	$(icons_DATA)			\
	$(appicons_DATA)

-include $(top_srcdir)/git.mk
