modlibexec_LTLIBRARIES = module-combine-sink.la

module_combine_sink_la_SOURCES = module-combine-sink.c

#AM_CFLAGS = -pedantic

module_combine_sink_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_combine_sink_la_LIBADD = $(AM_LIBADD) $(DBUS_LIBS) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_combine_sink_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)
