modlibexec_LTLIBRARIES = \
                  module-combine.la \
                  module-combine-sink.la

module_combine_sink_la_SOURCES = \
                  module-combine-sink.c \
                  module-combine.c

#AM_CFLAGS = -pedantic

module_combine_la_LDFLAGS =  -module -avoid-version -Wl,--no-undefined
module_combine_la_LIBADD = $(AM_LIBADD) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_combine_la_CFLAGS = $(AM_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)

module_combine_sink_la_LDFLAGS = -module -avoid-version -Wl,--no-undefined
module_combine_sink_la_LIBADD = $(AM_LIBADD) $(LIBPULSE_LIBS) $(PULSEDEVEL_LIBS)
module_combine_sink_la_CFLAGS = $(AM_CFLAGS) $(LIBPULSE_CFLAGS) $(PULSEDEVEL_CFLAGS)
