EXTRA_DIST =						\
	colorhug-backlight.sgml				\
	colorhug-ccmx.sgml				\
	colorhug-cmd.sgml				\
	colorhug-refresh.sgml				\
	colorhug-flash.sgml

if HAVE_DOCBOOK2MAN
man_MANS =						\
	colorhug-backlight.1				\
	colorhug-ccmx.1					\
	colorhug-cmd.1					\
	colorhug-refresh.1				\
	colorhug-flash.1
endif

if HAVE_DOCBOOK2MAN
colorhug-backlight.1: colorhug-backlight.sgml
	docbook2man $? > /dev/null
colorhug-ccmx.1: colorhug-ccmx.sgml
	docbook2man $? > /dev/null
colorhug-cmd.1: colorhug-cmd.sgml
	docbook2man $? > /dev/null
colorhug-flash.1: colorhug-flash.sgml
	docbook2man $? > /dev/null
colorhug-refresh.1: colorhug-refresh.sgml
	docbook2man $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

CLEANFILES =						\
	manpage.links					\
	manpage.log					\
	manpage.refs					\
	$(man_MANS)

-include $(top_srcdir)/git.mk
