AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(COLORD_CFLAGS)				\
	$(COLORHUG_CFLAGS)				\
	$(COLORD_GTK_CFLAGS)				\
	$(SOUP_CFLAGS)					\
	$(CANBERRA_CFLAGS)				\
	-DCD_DISABLE_DEPRECATED				\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-DG_LOG_DOMAIN=\"ChClient\"			\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DLOCALEDIR=\""$(localedir)"\"

bin_PROGRAMS =						\
	colorhug-spectro				\
	colorhug-backlight				\
	colorhug-ccmx					\
	colorhug-cmd					\
	colorhug-refresh

colorhug_cmd_SOURCES =					\
	ch-main.c

colorhug_cmd_LDADD =					\
	$(COLORD_LIBS)					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(SOUP_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_cmd_CFLAGS =					\
	$(WARNINGFLAGS_C)

if HAS_WINDOWS_ICON
ch-main-rc.$(OBJEXT) : $(top_builddir)/data/ch-main.rc
	$(AM_V_GEN)					\
	$(WINDRES)					\
	--input-format rc --input $<			\
	--output-format coff --output $@
colorhug_cmd_LDADD += ch-main-rc.$(OBJEXT)
colorhug_cmd_DEPENDENCIES = ch-main-rc.$(OBJEXT)
endif

colorhug_ccmx_SOURCES =					\
	ch-ccmx-resources.c				\
	ch-ccmx-resources.h				\
	ch-ccmx.c

colorhug_ccmx_LDADD =					\
	$(COLORD_LIBS)					\
	$(COLORD_GTK_LIBS)				\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(SOUP_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_ccmx_CFLAGS =					\
	$(WARNINGFLAGS_C)

if HAVE_WIN32_RELEASE
colorhug_ccmx_LDFLAGS =					\
	-mwindows
endif

if HAS_WINDOWS_ICON
ch-ccmx-rc.$(OBJEXT) : $(top_builddir)/data/ch-ccmx.rc
	$(AM_V_GEN)					\
	$(WINDRES)					\
	--input-format rc --input $<			\
	--output-format coff --output $@
colorhug_ccmx_LDADD += ch-ccmx-rc.$(OBJEXT)
colorhug_ccmx_DEPENDENCIES = ch-ccmx-rc.$(OBJEXT)
endif

colorhug_refresh_SOURCES =				\
	egg-graph-widget.c				\
	egg-graph-widget.h				\
	egg-graph-point.c					\
	egg-graph-point.h					\
	ch-refresh.c					\
	ch-refresh-resources.c				\
	ch-refresh-resources.h				\
	ch-refresh-utils.c				\
	ch-refresh-utils.h

if HAVE_WIN32_RELEASE
colorhug_refresh_LDFLAGS =				\
	-mwindows
endif

colorhug_refresh_LDADD =				\
	$(COLORD_LIBS)					\
	$(COLORD_GTK_LIBS)				\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_refresh_CFLAGS =				\
	$(WARNINGFLAGS_C)

colorhug_backlight_SOURCES =				\
	ch-ambient.c					\
	ch-ambient.h					\
	egg-graph-widget.c				\
	egg-graph-widget.h				\
	egg-graph-point.c					\
	egg-graph-point.h					\
	ch-backlight.c					\
	ch-backlight-resources.c			\
	ch-backlight-resources.h

if HAVE_WIN32_RELEASE
colorhug_backlight_LDFLAGS =				\
	-mwindows
endif

colorhug_backlight_LDADD =				\
	$(COLORD_LIBS)					\
	$(COLORD_GTK_LIBS)				\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_backlight_CFLAGS =				\
	$(WARNINGFLAGS_C)

colorhug_spectro_SOURCES =				\
	egg-graph-widget.c				\
	egg-graph-widget.h				\
	egg-graph-point.c					\
	egg-graph-point.h					\
	ch-spectro.c					\
	ch-spectro-resources.c				\
	ch-spectro-resources.h

if HAVE_WIN32_RELEASE
colorhug_spectro_LDFLAGS =				\
	-mwindows
endif

colorhug_spectro_LDADD =				\
	$(COLORD_LIBS)					\
	$(COLORD_GTK_LIBS)				\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_spectro_CFLAGS =				\
	$(WARNINGFLAGS_C)

if HAS_WINDOWS_ICON
ch-refresh-rc.$(OBJEXT) : $(top_builddir)/data/ch-refresh.rc
	$(AM_V_GEN)					\
	$(WINDRES) \
	--input-format rc --input $< \
	--output-format coff --output $@
colorhug_refresh_LDADD += ch-refresh-rc.$(OBJEXT)
colorhug_refresh_DEPENDENCIES = ch-refresh-rc.$(OBJEXT)
endif

ch-ccmx-resources.c: colorhug-ccmx.gresource.xml ../data/ch-ccmx.ui ../data/ch-ccmx-menus.ui
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-source				\
	--c-name ch					\
	$(srcdir)/colorhug-ccmx.gresource.xml
ch-ccmx-resources.h: colorhug-ccmx.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-header				\
	--c-name ch					\
	$(srcdir)/colorhug-ccmx.gresource.xml

ch-refresh-resources.c: colorhug-refresh.gresource.xml ../data/ch-refresh.ui ../data/ch-refresh-menus.ui
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-source				\
	--c-name ch					\
	$(srcdir)/colorhug-refresh.gresource.xml
ch-refresh-resources.h: colorhug-refresh.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-header				\
	--c-name ch					\
	$(srcdir)/colorhug-refresh.gresource.xml

ch-backlight-resources.c: colorhug-backlight.gresource.xml ../data/ch-backlight.ui ../data/ch-backlight-menus.ui
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-source				\
	--c-name ch					\
	$(srcdir)/colorhug-backlight.gresource.xml
ch-backlight-resources.h: colorhug-backlight.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-header				\
	--c-name ch					\
	$(srcdir)/colorhug-backlight.gresource.xml

ch-spectro-resources.c: colorhug-spectro.gresource.xml ../data/ch-spectro.ui ../data/ch-spectro-menus.ui
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-source				\
	--c-name ch					\
	$(srcdir)/colorhug-spectro.gresource.xml
ch-spectro-resources.h: colorhug-spectro.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-header				\
	--c-name ch					\
	$(srcdir)/colorhug-spectro.gresource.xml

BUILT_SOURCES =						\
	ch-backlight-resources.c			\
	ch-backlight-resources.h			\
	ch-spectro-resources.c				\
	ch-spectro-resources.h				\
	ch-ccmx-resources.c				\
	ch-ccmx-resources.h				\
	ch-refresh-resources.c				\
	ch-refresh-resources.h

EXTRA_DIST =						\
	colorhug-backlight.gresource.xml		\
	colorhug-spectro.gresource.xml			\
	colorhug-ccmx.gresource.xml			\
	colorhug-refresh.gresource.xml

MAINTAINERCLEANFILES =					\
	$(srcdir)/libchshared.a

if HAVE_UNIX
install-data-hook:
	mkdir -p $(DESTDIR)$(libexecdir);
	ln -fs $(bindir)/colorhug-cmd $(DESTDIR)$(libexecdir)/colorhug;
endif

TESTS_ENVIRONMENT =						\
	libtool --mode=execute valgrind				\
	--quiet							\
	--leak-check=full					\
	--show-possibly-lost=no

check_PROGRAMS =						\
	ch-self-test

ch_self_test_SOURCES =						\
	ch-self-test.c						\
	ch-refresh-utils.c					\
	ch-refresh-utils.h

ch_self_test_LDADD =						\
	$(COLORHUG_LIBS)					\
	$(GLIB_LIBS)						\
	$(GTK_LIBS)						\
	$(GUSB_LIBS)

ch_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS = ch-self-test

CLEANFILES = *.log *.trs

-include $(top_srcdir)/git.mk
