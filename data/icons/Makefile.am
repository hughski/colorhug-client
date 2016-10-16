SUBDIRS = scalable 16x16 22x22 24x24 32x32 48x48 64x64 256x256

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(pkgdatadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

if HAS_WINDOWS_ICON
noinst_DATA =								\
	colorhug-ccmx.ico						\
	colorhug-refresh.ico

colorhug-refresh.ico : $(top_srcdir)/data/icons/16x16/colorhug-refresh.png
	$(AM_V_GEN)							\
	$(ICOTOOL) -c							\
	$(top_srcdir)/data/icons/16x16/colorhug-refresh.png		\
	$(top_srcdir)/data/icons/32x32/colorhug-refresh.png		\
	$(top_srcdir)/data/icons/48x48/colorhug-refresh.png		\
	$(top_srcdir)/data/icons/64x64/colorhug-refresh.png		\
	-o $@
colorhug-ccmx.ico : $(top_srcdir)/data/icons/16x16/colorhug-ccmx.png
	$(AM_V_GEN)							\
	$(ICOTOOL) -c							\
	$(top_srcdir)/data/icons/16x16/colorhug-ccmx.png		\
	$(top_srcdir)/data/icons/32x32/colorhug-ccmx.png		\
	$(top_srcdir)/data/icons/48x48/colorhug-ccmx.png		\
	$(top_srcdir)/data/icons/64x64/colorhug-ccmx.png		\
	-o $@
endif

CLEANFILES = *.ico

-include $(top_srcdir)/git.mk
