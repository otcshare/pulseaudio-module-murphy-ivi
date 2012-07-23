modlibexec_LTLIBRARIES = module-augment-properties.la module-dir-watch.la

#AM_CFLAGS = -pedantic

#
# Module augment properties
#
module_augment_properties_la_SOURCES = module-augment-properties.c

module_augment_properties_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_augment_properties_la_LIBADD = $(AM_LIBADD) $(DBUS_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_augment_properties_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS) -DDESKTOPFILEDIR=\"/usr/share/applications\"


#
# Module dir watch
#
module_dir_watch_la_SOURCES = module-dir-watch.c
module_dir_watch_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_dir_watch_la_LIBADD  = $(AM_LIBADD) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_dir_watch_la_CFLAGS  = $(AM_CFLAGS)

