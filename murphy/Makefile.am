CONDITIONAL_LIBS   =
CONDITIONAL_CFLAGS =

if BUILD_WITH_DBUS
CONDITIONAL_LIBS   += $(DBUS_LIBS)
CONDITIONAL_CFLAGS += $(DBUS_CFLAGS)
ROUTERIF  = dbusif.c
AM_CFLAGS = -I.. -DWITH_DBUS
else
ROUTERIF = socketif.c
AM_CFLAGS = -I..
endif

CONDITIONAL_CFLAGS += -Wl,-rpath -Wl,$(modlibexecdir)

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
			audiomgr.c \
			$(ROUTERIF) \
			murphy-config.c \
			classify.c \
			utils.c \
			scripting.c \
			extapi.c \
			resource.c \
			murphyif.c

configdir = $(sysconfdir)/pulse
config_DATA = murphy-ivi.lua

EXTRA_DIST = $(config_DATA)

if BUILD_WITH_DBUS
dbussysdir = $(sysconfdir)/dbus-1/system.d
dbussys_DATA = pulseaudio-murphy-ivi.conf

EXTRA_DIST += $(dbussys_DATA)
endif

if BUILD_WITH_MURPHYIF
CONDITIONAL_CFLAGS += -DWITH_MURPHYIF
endif

if HAVE_AUL
CONDITIONAL_CFLAGS += -DWITH_AUL
endif


module_murphy_ivi_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined

module_murphy_ivi_la_LIBADD = $(AM_LIBADD) $(CONDITIONAL_LIBS)              \
                              $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)           \
                              $(MURPHYCOMMON_LIBS) $(MURPHYDOMCTL_LIBS)     \
                              $(LUAUTILS_LIBS) $(LUA_LIBS)                  \
                              $(AUL_LIBS) -lmain-volume-policy
module_murphy_ivi_la_CFLAGS = $(AM_CFLAGS) $(CONDITIONAL_CFLAGS)            \
                              $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)       \
                              $(MURPHYCOMMON_CFLAGS) $(MURPHYDOMCTL_CFLAGS) \
                              $(LUAUTILS_CFLAGS) $(LUA_CFLAGS)              \
                              $(AUL_CFLAGS)

