themedir = $(datadir)/icons/hicolor
size = 24x24
context = apps

iconsdir = $(themedir)/$(size)/$(context)

icons_DATA = 				\
	colorimeter-colorhug-inactive.png \
	colorhug-backlight.png		\
	colorhug-ccmx.png		\
	colorhug-refresh.png		\
	colorhug.png

install-data-hook:
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/colorhug.png $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes/application-x-ccmx.png;

EXTRA_DIST =		\
	$(icons_DATA)

-include $(top_srcdir)/git.mk
