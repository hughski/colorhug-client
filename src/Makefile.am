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

noinst_PROGRAMS =					\
	colorhug-spectro-util

colorhug_spectro_util_SOURCES =				\
	ch-graph-widget.c				\
	ch-graph-widget.h				\
	ch-point-obj.c					\
	ch-point-obj.h					\
	ch-spectro-resources.c				\
	ch-spectro-resources.h				\
	ch-spectro-util.c

colorhug_spectro_util_LDADD =				\
	$(COLORD_LIBS)					\
	$(COLORD_GTK_LIBS)				\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_spectro_util_CFLAGS =				\
	$(WARNINGFLAGS_C)

libexec_PROGRAMS =					\
	colorhug-inhx32-to-bin

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

colorhug_inhx32_to_bin_SOURCES =			\
	ch-cleanup.h					\
	ch-inhx32-to-bin.c

colorhug_inhx32_to_bin_LDADD =				\
	$(GLIB_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_inhx32_to_bin_CFLAGS =				\
	$(WARNINGFLAGS_C)

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

ch-flash-resources.c: colorhug-flash.gresource.xml ../data/ch-flash.ui
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-source --c-name ch $(srcdir)/colorhug-flash.gresource.xml
ch-flash-resources.h: colorhug-flash.gresource.xml
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-header --c-name ch $(srcdir)/colorhug-flash.gresource.xml

ch-ccmx-resources.c: colorhug-ccmx.gresource.xml ../data/ch-ccmx.ui
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-source --c-name ch $(srcdir)/colorhug-ccmx.gresource.xml
ch-ccmx-resources.h: colorhug-ccmx.gresource.xml
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-header --c-name ch $(srcdir)/colorhug-ccmx.gresource.xml

ch-spectro-resources.c: colorhug-spectro.gresource.xml ../data/ch-spectro-util.ui
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-source --c-name ch $(srcdir)/colorhug-spectro.gresource.xml
ch-spectro-resources.h: colorhug-spectro.gresource.xml
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-header --c-name ch $(srcdir)/colorhug-spectro.gresource.xml

ch-refresh-resources.c: colorhug-refresh.gresource.xml ../data/ch-refresh.ui ../data/ch-refresh-menus.ui
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-source --c-name ch $(srcdir)/colorhug-refresh.gresource.xml
ch-refresh-resources.h: colorhug-refresh.gresource.xml
	glib-compile-resources --target=$@ --sourcedir=$(top_srcdir)/data --generate-header --c-name ch $(srcdir)/colorhug-refresh.gresource.xml

BUILT_SOURCES =						\
	ch-ccmx-resources.c				\
	ch-ccmx-resources.h				\
	ch-flash-resources.c				\
	ch-flash-resources.h				\
	ch-refresh-resources.c				\
	ch-refresh-resources.h				\
	ch-spectro-resources.c				\
	ch-spectro-resources.h

EXTRA_DIST =						\
	colorhug-ccmx.gresource.xml			\
	colorhug-flash.gresource.xml			\
	colorhug-refresh.gresource.xml			\
	colorhug-spectro.gresource.xml

MAINTAINERCLEANFILES =					\
	$(srcdir)/libchshared.a

install-data-hook:
	mkdir -p $(DESTDIR)$(libexecdir);
	ln -fs $(bindir)/colorhug-cmd $(DESTDIR)$(libexecdir)/colorhug;

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
	$(GUSB_LIBS)

ch_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS = ch-self-test

CLEANFILES = *.log *.trs

-include $(top_srcdir)/git.mk
