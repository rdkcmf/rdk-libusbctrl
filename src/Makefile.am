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
lib_LTLIBRARIES = libusbctrl.la
libusbctrl_la_SOURCES = usbctrl.cpp
libusbctrl_la_CPPFLAGS = -I$(top_srcdir)/include -I${RDK_FSROOT_PATH}/include -I${RDK_FSROOT_PATH}/usr/include
libusbctrl_la_LDFLAGS = -ludev -lpthread
include_HEADERS = $(top_srcdir)/include/usbctrl.h

if ENABLE_TESTAPP 
bin_PROGRAMS = usbctrltestapp
usbctrltestapp_SOURCES = usbtest.cpp
usbctrltestapp_CPPFLAGS = -I$(top_srcdir)/include
usbctrltestapp_LDADD = libusbctrl.la
endif
