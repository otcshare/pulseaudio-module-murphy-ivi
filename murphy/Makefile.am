modlibexec_LTLIBRARIES = module-murphy-ivi.la

module_murphy_ivi_la_SOURCES = \
			module-murphy-ivi.c \
			tracker.c \
			node.c \
			discover.c \
			router.c \
			switch.c \
			multiplex.c \
			audiomgr.c \
			dbusif.c \
			config.c \
			classify.c \
			utils.c

AM_CFLAGS = -I..

module_murphy_ivi_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_murphy_ivi_la_LIBADD = $(AM_LIBADD) $(DBUS_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_murphy_ivi_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)
