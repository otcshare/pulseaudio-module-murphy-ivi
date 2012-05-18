modlibexec_LTLIBRARIES = module-genivi-mir.la

module_genivi_mir_la_SOURCES = \
			module-genivi-mir.c \
			index-hash.c \
			config-file.c \
			client-ext.c \
			sink-ext.c \
			source-ext.c \
			sink-input-ext.c \
			source-output-ext.c \
			card-ext.c \
			module-ext.c \
			classify.c \
			discover.c \
			node.c \
			policy-group.c \
			context.c \
			audiomgr.c \
			dbusif.c

AM_CFLAGS = -g3 -O0

module_genivi_mir_la_LDFLAGS = -module -avoid-version
module_genivi_mir_la_LIBADD = $(AM_LIBADD) $(DBUS_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_genivi_mir_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS) -DPULSEAUDIO_HAS_PORTS=$(PULSEAUDIO_HAS_PORTS)
