if BUILD_WITH_DBUS
CONDITIONAL_LIBS    = $(DBUS_LIBS)
CONDITIONAL_CFLAGS = $(DBUS_CFLAGS)
ROUTERIF  = dbusif.c
AM_CFLAGS = -I.. -DWITH_DBUS
else
ROUTERIF = socketif.c
AM_CFLAGS = -I..
endif

modlibexec_LTLIBRARIES = module-murphy-ivi.la

module_murphy_ivi_la_SOURCES = \
			module-murphy-ivi.c \
			tracker.c \
			node.c \
			discover.c \
			constrain.c \
			router.c \
			switch.c \
			fader.c \
			multiplex.c \
			loopback.c \
			volume.c \
			audiomgr.c \
			$(ROUTERIF) \
			config.c \
			classify.c \
			utils.c

if BUILD_WITH_DBUS
dbussysdir = $(sysconfdir)/dbus-1/system.d
dbussys_DATA = pulseaudio-murphy-ivi.conf

EXTRA_DIST = $(dbussys_DATA)
endif


module_murphy_ivi_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_murphy_ivi_la_LIBADD = $(AM_LIBADD) $(CONDITIONAL_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_murphy_ivi_la_CFLAGS = $(AM_CFLAGS) $(CONDITIONAL_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)
