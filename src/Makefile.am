INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(COLORD_CFLAGS)				\
	$(LCMS_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(SQLITE_CFLAGS)					\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DCH_DATA=\"$(pkgdatadir)\"			\
	-DLOCALEDIR=\""$(localedir)"\"

noinst_LIBRARIES = libchshared.a
libchshared_a_SOURCES =					\
	ch-math.c					\
	ch-math.h					\
	ch-client.c					\
	ch-client.h					\
	ch-common.c					\
	ch-common.h

libchshared_a_CFLAGS =					\
	$(WARNINGFLAGS_C)

libexec_PROGRAMS =					\
	colorhug					\
	colorhug-inhx32-to-bin				\
	colorhug-gui

bin_PROGRAMS =						\
	colorhug-ccmx					\
	colorhug-flash

colorhug_SOURCES =					\
	ch-main.c

colorhug_LDADD =					\
	libchshared.a					\
	$(LCMS_LIBS)					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(SQLITE_LIBS)					\
	-lm

colorhug_CFLAGS =					\
	$(WARNINGFLAGS_C)

colorhug_gui_SOURCES =					\
	ch-util.c

colorhug_gui_LDADD =					\
	libchshared.a					\
	$(COLORD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(LCMS_LIBS)					\
	-lm

colorhug_gui_CFLAGS =					\
	$(WARNINGFLAGS_C)

colorhug_flash_SOURCES =				\
	ch-markdown.c					\
	ch-markdown.h					\
	ch-flash.c

colorhug_flash_LDADD =					\
	libchshared.a					\
	$(COLORD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(SOUP_LIBS)					\
	$(LCMS_LIBS)					\
	-lm

colorhug_flash_CFLAGS =					\
	$(WARNINGFLAGS_C)

colorhug_ccmx_SOURCES =					\
	ch-ccmx.c

colorhug_ccmx_LDADD =					\
	libchshared.a					\
	$(COLORD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(SOUP_LIBS)					\
	$(LCMS_LIBS)					\
	-lm

colorhug_ccmx_CFLAGS =					\
	$(WARNINGFLAGS_C)

colorhug_inhx32_to_bin_SOURCES =			\
	ch-inhx32-to-bin.c

colorhug_inhx32_to_bin_LDADD =				\
	$(GLIB_LIBS)					\
	-lm

colorhug_inhx32_to_bin_CFLAGS =				\
	$(WARNINGFLAGS_C)

check_PROGRAMS =					\
	ch-self-test

ch_self_test_SOURCES =					\
	ch-self-test.c

ch_self_test_LDADD =					\
	libchshared.a					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(LCMS_LIBS)					\
	-lm

ch_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = ch-self-test

MAINTAINERCLEANFILES =					\
	$(srcdir)/libchshared.a

-include $(top_srcdir)/git.mk
