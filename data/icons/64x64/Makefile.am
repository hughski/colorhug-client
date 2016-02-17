themedir = $(datadir)/icons/hicolor
size = 64x64
context = apps

iconsdir = $(themedir)/$(size)/$(context)

icons_DATA = 					\
	colorhug.png

if BUILD_COLORHUG_CCMX
icons_DATA += colorhug-ccmx.png
endif

if BUILD_COLORHUG_BACKLIGHT
icons_DATA += colorhug-backlight.png
endif

if BUILD_COLORHUG_FLASH
icons_DATA += colorhug-flash.png
endif

if BUILD_COLORHUG_REFRESH
icons_DATA += colorhug-refresh.png
endif

install-data-hook:
	mkdir -p $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes;
	ln -fs $(iconsdir)/colorhug.png $(DESTDIR)$(datadir)/icons/hicolor/$(size)/mimetypes/application-x-ccmx.png;

EXTRA_DIST =		\
	$(icons_DATA)

-include $(top_srcdir)/git.mk
