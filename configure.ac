##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([libusbctrl], [0.10], [chaithen_sojan@cable.comcast.com])
AC_CONFIG_SRCDIR([src/usbctrl.cpp])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE
LT_INIT
LT_LANG([C++])

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([select])

AC_ARG_ENABLE([testapp],
              AS_HELP_STRING([--enable-testapp],[enable testapp]),
              [testapp=true;echo "testapp is enabled";],
              [testapp=false;echo "testapp is disabled";])
AM_CONDITIONAL([ENABLE_TESTAPP], [test x$testapp = xtrue])
AC_CONFIG_FILES([Makefile
				src/Makefile])
AC_OUTPUT
