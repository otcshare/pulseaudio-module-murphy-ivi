m4_define(FULL_PAVER, esyscmd(/usr/bin/pkg-config --silence-errors --modversion pulsecore | tr -d \\n))
m4_define(PAVER, regexp(FULL_PAVER, [\([0123456789.]+\).*], [\1]))

AC_PREREQ(2.61)

AC_INIT([pulseaudio-murphy-ivi],[PAVER],[janos.kovacs@intel.com])

DOC_SUPPORT_INIT()

AC_CONFIG_SRCDIR([murphy/module-murphy-ivi.c])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign 1.8.5 -Wall])

# this would not be really needed if everything would be as it should be
AS_IF([test x${exec_prefix} = xNONE], [LIBDIR=$ac_default_prefix/lib], [LIBDIR=${libdir}])
AC_SUBST(LIBDIR)

AC_SUBST(PA_MAJORMINOR, [PAVER])

AC_PROG_MKDIR_P
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AC_USE_SYSTEM_EXTENSIONS

AC_PATH_PROG([M4], [m4 gm4], [no])
if test "x$M4" = xno ; then
   AC_MSG_ERROR([m4 missing])
fi

#dnl Compiler flags
#DESIRED_FLAGS="-Wall -W -Wextra -pedantic -pipe -Wno-long-long -Wvla -Wno-overlength-strings -Wconversion -Wundef -Wformat -Wlogical-op -Wpacked -Wformat-security -Wmissing-include-dirs -Wformat-nonliteral -Wold-style-definition -Wdeclaration-after-statement -Wfloat-equal -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wmissing-noreturn -Wshadow -Wendif-labels -Wpointer-arith -Wcast-align -Wwrite-strings -Wno-unused-parameter -ffast-math"

#for flag in $DESIRED_FLAGS ; do
#  CC_CHECK_CFLAGS([$flag], [CFLAGS="$CFLAGS $flag"])
#done

AC_LTDL_ENABLE_INSTALL
AC_LIBLTDL_INSTALLABLE
AC_LIBTOOL_DLOPEN
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL
AC_SUBST(LTDLINCL)
AC_SUBST(LIBLTDL)
AC_CONFIG_SUBDIRS(libltdl)

AC_HEADER_STDC
AC_C_CONST
AC_C_BIGENDIAN
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_TYPES(ssize_t, , [AC_DEFINE([ssize_t], [signed long],
    [Define ssize_t if it is not done by the standard libs.])])
AC_TYPE_OFF_T
AC_TYPE_SIGNAL
AC_TYPE_UID_T
AC_CHECK_DECLS(environ)

AC_DEFINE_UNQUOTED(PA_CFLAGS,"$CFLAGS", [The CFLAGS used during compilation])

PKG_CHECK_MODULES(LIBPULSE, [libpulse])
AC_SUBST(LIBPULSE_CFLAGS)
AC_SUBST(LIBPULSE_LIBS)

#PKG_CHECK_MODULES(LIBPULSECORE, [pulsecore])
#AC_SUBST(LIBPULSECORE_CFLAGS)
#AC_SUBST(LIBPULSECORE_LIBS)

PKG_CHECK_MODULES(PULSEDEVEL, [pulseaudio-module-devel])
AC_SUBST(PULSEDEVEL_CFLAGS)
AC_SUBST(PULSEDEVEL_LIBS)


PKG_CHECK_MODULES(DBUS, [dbus-1])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)


AC_ARG_WITH(
        [module-dir],
        AS_HELP_STRING([--with-module-dir],[Directory where to install the modules to (defaults to ${LIBDIR}/pulse-${PA_MAJORMINOR}/modules/]),
        [modlibexecdir=$withval], [modlibexecdir=${LIBDIR}/pulse-${PA_MAJORMINOR}/modules])


AC_SUBST(modlibexecdir)

AC_CONFIG_FILES([
	Makefile
	murphy/Makefile
	doc/Makefile
])
AC_OUTPUT

echo "
 ---{ $PACKAGE_NAME $VERSION }---

    prefix:               ${prefix}
    sysconfdir:           ${sysconfdir}
    localstatedir:        ${localstatedir}
    modlibexecdir:        ${modlibexecdir}
    Compiler:             ${CC}
    CFLAGS:               ${CFLAGS}
    LIBDIR:               ${LIBDIR}
    LIBPULSE_CFLAGS:      ${LIBPULSE_CFLAGS}
    LIBPULSE_LIBS:        ${LIBPULSE_LIBS}
    LIBPULSECORE_CFLAGS:  ${LIBPULSECORE_CFLAGS}
    LIBPULSECORE_LIBS:    ${LIBPULSECORE_LIBS}
    MAJORMINOR:           ${PA_MAJORMINOR}
    DBUS_CFLAGS:          ${DBUS_CFLAGS}
    DBUS_LIBS:            ${DBUS_LIBS}
    PD_SUPPORT:           ${PD_SUPPORT}
"
