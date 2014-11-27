themedir = $(datadir)/icons/hicolor
size = 64x64
context = apps

iconsdir = $(themedir)/$(size)/$(context)

icons_DATA = 				\
	colorhug-ccmx.png		\
	colorhug-flash.png		\
	colorhug-refresh.png		\
	colorhug.png

install-data-hook:
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/colorhug.png $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes/application-x-ccmx.png;

EXTRA_DIST =		\
	$(icons_DATA)

-include $(top_srcdir)/git.mk
