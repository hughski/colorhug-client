AUTOMAKE_OPTIONS = 1.7

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =						\
	COPYING						\
	MAINTAINERS					\
	AUTHORS						\
	INSTALL						\
	README						\
	NEWS						\
	autogen.sh					\
	config.h

MAINTAINERCLEANFILES =					\
	$(srcdir)/INSTALL				\
	$(srcdir)/aclocal.m4				\
	$(srcdir)/autoscan.log				\
	$(srcdir)/compile				\
	$(srcdir)/config.guess				\
	$(srcdir)/config.rpath				\
	$(srcdir)/config.h.in				\
	$(srcdir)/config.sub				\
	$(srcdir)/configure.scan			\
	$(srcdir)/depcomp				\
	$(srcdir)/install-sh				\
	$(srcdir)/ltmain.sh				\
	$(srcdir)/missing				\
	$(srcdir)/mkinstalldirs				\
	$(srcdir)/omf.make				\
	$(srcdir)/ChangeLog				\
	$(srcdir)/colorhug-*.tar.*			\
	`find "$(srcdir)" -type f -name Makefile.in -print`

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log e197d80c8937c622f21f569457d1bbd05746d951.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	-DG_LOG_DOMAIN=\"Ch\"				\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE

noinst_PROGRAMS =					\
	colorhug

colorhug_SOURCES =					\
	ch-common.h					\
	ch-client.c					\
	ch-client.h					\
	ch-main.c

colorhug_LDADD =					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)

colorhug_CFLAGS =					\
	$(WARNINGFLAGS_C)

check_PROGRAMS =					\
	ch-self-test

ch_self_test_SOURCES =					\
	ch-self-test.c

ch_self_test_LDADD =					\
	$(GUSB_LIBS)					\
	$(GLIB_LIBS)

ch_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = ch-self-test

-include $(top_srcdir)/git.mk
