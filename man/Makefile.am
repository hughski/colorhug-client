EXTRA_DIST =						\
	colorhug-flash.sgml \
	colorhug-ccmx.sgml

if HAVE_DOCBOOK2MAN
man_MANS =						\
	colorhug-flash.1 \
	colorhug-ccmx.1
endif

if HAVE_DOCBOOK2MAN
colorhug-flash.1: colorhug-flash.sgml
	docbook2man $? > /dev/null
colorhug-ccmx.1: colorhug-ccmx.sgml
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
