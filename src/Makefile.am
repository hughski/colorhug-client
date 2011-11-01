INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(COLORD_CFLAGS)				\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DCH_DATA=\"$(pkgdatadir)\"			\
	-DLOCALEDIR=\""$(localedir)"\"

noinst_LIBRARIES = libchshared.a
libchshared_a_SOURCES =					\
	ch-client.c					\
	ch-client.h					\
	ch-common.h

libchshared_a_CFLAGS =					\
	$(WARNINGFLAGS_C)

noinst_PROGRAMS =					\
	colorhug					\
	colorhug-gui

colorhug_SOURCES =					\
	ch-main.c

colorhug_LDADD =					\
	libchshared.a					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)

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
	-lm

colorhug_gui_CFLAGS =					\
	$(WARNINGFLAGS_C)

check_PROGRAMS =					\
	ch-self-test

ch_self_test_SOURCES =					\
	ch-self-test.c

ch_self_test_LDADD =					\
	libchshared.a					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)

ch_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = ch-self-test

pkgdata_DATA =						\
	ch-util.ui

EXTRA_DIST =						\
	$(pkgdata_DATA)

-include $(top_srcdir)/git.mk
