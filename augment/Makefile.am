modlibexec_LTLIBRARIES = module-augment-properties.la

module_augment_properties_la_SOURCES = module-augment-properties.c

#AM_CFLAGS = -pedantic

module_augment_properties_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_augment_properties_la_LIBADD = $(AM_LIBADD) $(DBUS_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_augment_properties_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS) -DDESKTOPFILEDIR=\"/usr/share/applications\"
