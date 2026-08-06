dnl
dnl Configuration script for Mike's System Monitor
dnl
dnl Copyright © 2026 by Michael R Sweet
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl ***********************************************************************
dnl
dnl Note: Using autoheader or automake on this project will break the msysmon
dnl build system.  Use "autoconf -f" to regenerate the configure script if
dnl you make changes to this file.
dnl
dnl ***********************************************************************


dnl We need at least autoconf 2.70 for --runstatedir...
AC_PREREQ([2.70])


dnl Package name and version...
AC_INIT([msysmon], [0.1.0], [https://github.com/michaelrsweet/msysmon/issues], [msysmon], [https://www.msweet.org/msysmon/])

VERSION="AC_PACKAGE_VERSION"
AC_SUBST([VERSION])


dnl Get the build and host platforms and split the host_os value
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

[host_os_name="$(echo $host_os | sed -e '1,$s/[0-9.]*$//g')"]
[host_os_version="$(echo $host_os | sed -e '1,$s/^[^0-9.]*//g' | awk -F. '{print $1 $2}')"]
# Linux often does not yield an OS version we can use...
AS_IF([test "x$host_os_version" = x], [
    host_os_version="0"
])


dnl Compiler options...
CFLAGS="${CFLAGS:=}"
CPPFLAGS="${CPPFLAGS:=}"
LDFLAGS="${LDFLAGS:=}"
LIBS="${LIBS:=}"
OPTIM="${OPTIM:=}"

AC_SUBST([OPTIM])


dnl Standard programs...
AC_PROG_CC
AC_PATH_PROG([RM], [rm])


dnl install-sh
AC_MSG_CHECKING([for install-sh script])
INSTALL="$(pwd)/install-sh"
AC_SUBST([INSTALL])
AC_MSG_RESULT([using $INSTALL])


dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_TOOL([PKGCONFIG], [pkg-config])

dnl TTF library for font support...
AC_MSG_CHECKING([for libcups v3 library])
AS_IF([$PKGCONFIG --exists cups3], [
    AC_MSG_RESULT([yes])
    CPPFLAGS="$CPPFLAGS $($PKGCONFIG --cflags cups3)"
    LIBS="$($PKGCONFIG --libs cups3) $LIBS"
], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Sorry, this software requires libcups 3.0 or higher.])
])


dnl Extra compiler options...
AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [turn on debugging, default=no]))
AC_ARG_ENABLE([maintainer], AS_HELP_STRING([--enable-maintainer], [turn on maintainer mode, default=no]))
AC_ARG_ENABLE([sanitizer], AS_HELP_STRING([--enable-sanitizer], [build with AddressSanitizer, default=no]))

AS_IF([test x$enable_debug = xyes], [
    OPTIM="$OPTIM -g"
    CSFLAGS=""
], [
    OPTIM="$OPTIM -g -Os"
    CSFLAGS="-o runtime"
])

AC_SUBST([CSFLAGS])

WARNINGS=""
AC_SUBST([WARNINGS])

AS_IF([test -n "$GCC"], [
    AS_IF([test x$enable_sanitizer = xyes], [
	# Use -fsanitize=address with debugging...
	OPTIM="$OPTIM -fsanitize=address"
    ], [
	# Otherwise use the Fortify enhancements to catch any unbounded
	# string operations...
	CPPFLAGS="$CPPFLAGS -D_FORTIFY_SOURCE=2"
    ])

    dnl Show all standard warnings + unused variables when compiling...
    WARNINGS="-Wall -Wunused"

    dnl Drop some not-useful/unreliable warnings...
    for warning in char-subscripts format-truncation format-y2k switch unused-result; do
	AC_MSG_CHECKING([whether compiler supports -Wno-$warning])

	OLDCFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Wno-$warning -Werror"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [
	    AC_MSG_RESULT(yes)
	    WARNINGS="$WARNINGS -Wno-$warning"
        ], [
	    AC_MSG_RESULT(no)
	])

	CFLAGS="$OLDCFLAGS"
    done

    dnl Maintainer mode enables -Werror...
    AS_IF([test x$enable_maintainer = xyes], [
	WARNINGS="$WARNINGS -Werror -Wno-error=deprecated"
    ])

    dnl See if PIE options are supported...
    AC_MSG_CHECKING(whether compiler supports -fPIE)
    OLDCFLAGS="$CFLAGS"
    AS_CASE(["$host_os_name"],
    [darwin*], [
	CFLAGS="$CFLAGS -fPIC -fPIE -Wl,-pie"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[
	    OLDCFLAGS="-fPIC $OLDCFLAGS"
	    LDFLAGS="-fPIE -Wl,-pie $LDFLAGS"
	    AC_MSG_RESULT(yes)
	],[
	    AC_MSG_RESULT(no)
	])
    ], [*], [
	CFLAGS="$CFLAGS -fPIC -fPIE -pie"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[
	    OLDCFLAGS="-fPIC $OLDCFLAGS"
	    LDFLAGS="-fPIE -pie $LDFLAGS"
	    AC_MSG_RESULT(yes)
	],[
	    AC_MSG_RESULT(no)
	])
    ])
    CFLAGS="$OLDCFLAGS"

    dnl OS-specific compiler options...
    AC_MSG_CHECKING([for OS-specific compiler options])
    AS_CASE(["$host_os_name"], [linux*], [
    	# Make sure we get the full set of 64-bit Linux APIs from the headers...
	CPPFLAGS="$CPPFLAGS -D__USE_MISC -D_GNU_SOURCE -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"

	# Mark read-only sections as relocatable to random addresses...
	LDFLAGS="$LDFLAGS -Wl,-z,relro,-z,now"

        AC_MSG_RESULT([-D__USE_MISC -D_GNU_SOURCE -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 -Wl,-z,relro,-z,now])
    ], [darwin*], [
        # When not building for debug, target macOS 11 or later, "universal"
        # binaries when possible...
        AS_IF([echo "$CPPFLAGS $CFLAGS $LDFLAGS $OPTIM" | grep -q "\\-arch "], [
            # Don't add architecture/min-version flags if they are already present
            AC_MSG_RESULT([none])
        ], [echo "$CPPFLAGS $CFLAGS $LDFLAGS $OPTIM" | grep -q "\\-mmacosx-version-"], [
            # Don't add architecture/min-version flags if they are already present
            AC_MSG_RESULT([none])
        ], [test "$host_os_version" -ge 200 -a x$enable_debug != xyes], [
	    # macOS 11.0 and higher support the Apple Silicon (arm64) CPUs
	    OPTIM="$OPTIM -mmacosx-version-min=11.0 -arch x86_64 -arch arm64"
            AC_MSG_RESULT([-mmacosx-version-min=11.0 -arch x86_64 -arch arm64])
	], [
	    # Don't add architecture/min-version flags if debug enabled
            AC_MSG_RESULT([none])
	])
    ], [*], [
        AC_MSG_RESULT([none])
    ])
])


dnl Extra linker options...
AC_ARG_WITH([ldflags], AS_HELP_STRING([--with-ldflags=...], [Specify additional LDFLAGS]), [
    LDFLAGS="$withval $LDFLAGS"
])


dnl Generate the Makefile...
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
