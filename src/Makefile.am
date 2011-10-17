INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DLOCALEDIR=\""$(localedir)"\"

noinst_PROGRAMS =					\
	colorhug

colorhug_SOURCES =					\
	ch-client.c					\
	ch-client.h					\
	ch-common.h					\
	ch-main.c

colorhug_LDADD =					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)

colorhug_CFLAGS =					\
	$(WARNINGFLAGS_C)

check_PROGRAMS =					\
	ch-self-test

ch_self_test_SOURCES =					\
	ch-client.c					\
	ch-client.h					\
	ch-common.h					\
	ch-self-test.c

ch_self_test_LDADD =					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)

ch_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = ch-self-test

-include $(top_srcdir)/git.mk
