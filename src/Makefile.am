INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(COLORD_CFLAGS)				\
	$(LCMS_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(CANBERRA_CFLAGS)				\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE		\
	-DCH_DATA=\"$(pkgdatadir)\"			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-I$(top_srcdir)/libcolorhug

COLORHUG_LIBS =						\
	$(top_builddir)/libcolorhug/libcolorhug.la

libexec_PROGRAMS =					\
	colorhug					\
	colorhug-inhx32-to-bin

bin_PROGRAMS =						\
	colorhug-ccmx					\
	colorhug-flash

colorhug_SOURCES =					\
	ch-main.c

colorhug_LDADD =					\
	$(COLORD_LIBS)					\
	$(LCMS_LIBS)					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)					\
	$(SOUP_LIBS)					\
	$(COLORHUG_LIBS)				\
	-lm

colorhug_CFLAGS =					\
	$(WARNINGFLAGS_C)

colorhug_flash_SOURCES =				\
	ch-markdown.c					\
	ch-markdown.h					\
	ch-flash-md.c					\
	ch-flash-md.h					\
	ch-flash.c

colorhug_flash_LDADD =					\
	$(COLORD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(SOUP_LIBS)					\
	$(LCMS_LIBS)					\
	$(COLORHUG_LIBS)				\
	$(CANBERRA_LIBS)				\
	-lm

colorhug_flash_CFLAGS =					\
	$(WARNINGFLAGS_C)

colorhug_ccmx_SOURCES =					\
	ch-ccmx.c

colorhug_ccmx_LDADD =					\
	$(COLORD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(GUSB_LIBS)					\
	$(SOUP_LIBS)					\
	$(LCMS_LIBS)					\
	$(COLORHUG_LIBS)				\
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

-include $(top_srcdir)/git.mk
