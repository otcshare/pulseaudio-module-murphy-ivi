CONDITIONAL_LIBS   =
CONDITIONAL_CFLAGS =

CONDITIONAL_CFLAGS += -Wl,-rpath -Wl,$(modlibexecdir)

AM_CFLAGS = -I${top_srcdir}

modlibexec_LTLIBRARIES = module-murphy-ivi.la

module_murphy_ivi_la_SOURCES = \
			module-murphy-ivi.c \
			tracker.c \
			zone.c \
			node.c \
			discover.c \
			constrain.c \
			router.c \
			switch.c \
			fader.c \
			stream-state.c \
			multiplex.c \
			loopback.c \
			volume.c \
			murphy-config.c \
			classify.c \
			utils.c \
			scripting.c \
			extapi.c \
			resource.c \
			murphyif.c

configdir = $(sysconfdir)/pulse
config_DATA = murphy-ivi.lua

if HAVE_MURPHY
CONDITIONAL_CFLAGS += -DHAVE_MURPHY
endif

EXTRA_DIST = $(config_DATA)

module_murphy_ivi_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined

module_murphy_ivi_la_LIBADD = $(AM_LIBADD) $(CONDITIONAL_LIBS)              \
                              $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)           \
                              $(MURPHY_LIBS) $(MURPHY_LIBS)

module_murphy_ivi_la_CFLAGS = $(AM_CFLAGS) $(CONDITIONAL_CFLAGS)            \
                              $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)       \
                              $(MURPHY_CFLAGS) $(MURPHY_CFLAGS)
