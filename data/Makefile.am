SUBDIRS = 						\
	icons

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = colorhug-completion.bash

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	colorhug-ccmx.desktop.in			\
	colorhug-flash.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

pkgdata_DATA =						\
	ch-ccmx.ui					\
	ch-flash.ui

EXTRA_DIST =						\
	$(desktop_in_files)				\
	$(pkgdata_DATA)

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	colorhug-ccmx.desktop				\
	colorhug-flash.desktop

-include $(top_srcdir)/git.mk
