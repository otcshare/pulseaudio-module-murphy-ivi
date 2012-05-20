modlibexec_LTLIBRARIES = module-genivi-mir.la

module_genivi_mir_la_SOURCES = \
			module-genivi-mir.c \
			tracker.c \
			node.c \
			discover.c \
			audiomgr.c \
			dbusif.c \
			utils.c

AM_CFLAGS = -g3 -O0

module_genivi_mir_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_genivi_mir_la_LIBADD = $(AM_LIBADD) $(DBUS_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_genivi_mir_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS) -DPULSEAUDIO_HAS_PORTS=$(PULSEAUDIO_HAS_PORTS)
