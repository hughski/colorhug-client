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
	colorhug-ccmx					\
	colorhug-cmd					\
	colorhug-flash					\
	colorhug-refresh

colorhug_cmd_SOURCES =					\
	ch-cleanup.h					\
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

colorhug_flash_SOURCES =				\
	ch-cleanup.h					\
	ch-markdown.c					\
	ch-markdown.h					\
	ch-flash-resources.c				\
	ch-flash-resources.h				\
	ch-flash-md.c					\
	ch-flash-md.h					\
	ch-flash.c

colorhug_flash_LDADD =					\
	$(COLORD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(SOUP_LIBS)					\
	$(COLORHUG_LIBS)				\
	$(CANBERRA_LIBS)				\
	-lm

colorhug_flash_CFLAGS =					\
	$(WARNINGFLAGS_C)

if HAVE_WIN32_RELEASE
colorhug_flash_LDFLAGS =				\
	-mwindows
endif

if HAS_WINDOWS_ICON
ch-flash-rc.$(OBJEXT) : $(top_builddir)/data/ch-flash.rc
	$(AM_V_GEN)					\
	$(WINDRES)					\
	--input-format rc --input $<			\
	--output-format coff --output $@
colorhug_flash_LDADD += ch-flash-rc.$(OBJEXT)
colorhug_flash_DEPENDENCIES = ch-flash-rc.$(OBJEXT)
endif

colorhug_ccmx_SOURCES =					\
	ch-cleanup.h					\
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
	ch-cleanup.h					\
	ch-graph-widget.c				\
	ch-graph-widget.h				\
	ch-point-obj.c					\
	ch-point-obj.h					\
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

if HAS_WINDOWS_ICON
ch-refresh-rc.$(OBJEXT) : $(top_builddir)/data/ch-refresh.rc
	$(AM_V_GEN)					\
	$(WINDRES) \
	--input-format rc --input $< \
	--output-format coff --output $@
colorhug_refresh_LDADD += ch-refresh-rc.$(OBJEXT)
colorhug_refresh_DEPENDENCIES = ch-refresh-rc.$(OBJEXT)
endif

ch-flash-resources.c: colorhug-flash.gresource.xml ../data/ch-flash.ui
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-source				\
	--c-name ch					\
	$(srcdir)/colorhug-flash.gresource.xml
ch-flash-resources.h: colorhug-flash.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources --target=$@		\
	--sourcedir=$(top_srcdir)/data			\
	--generate-header				\
	--c-name ch					\
	$(srcdir)/colorhug-flash.gresource.xml

ch-ccmx-resources.c: colorhug-ccmx.gresource.xml ../data/ch-ccmx.ui
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

BUILT_SOURCES =						\
	ch-ccmx-resources.c				\
	ch-ccmx-resources.h				\
	ch-flash-resources.c				\
	ch-flash-resources.h				\
	ch-refresh-resources.c				\
	ch-refresh-resources.h

EXTRA_DIST =						\
	colorhug-ccmx.gresource.xml			\
	colorhug-flash.gresource.xml			\
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
