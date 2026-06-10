# GW-BASIC style interpreter — single source file, links against ncursesw.
#
# If libncurses-dev is not installed system-wide (and you have no root), the
# headers can be fetched locally without sudo:
#   mkdir -p .deps && cd .deps \
#     && apt-get download libncurses-dev \
#     && dpkg -x libncurses-dev_*.deb extracted
# The rules below pick the local copy up automatically.

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall

LOCAL_NC := $(wildcard .deps/extracted/usr/include)
ifneq ($(LOCAL_NC),)
CXXFLAGS += -I$(LOCAL_NC)
LDLIBS    = -l:libncursesw.so.6 -l:libtinfo.so.6
else
LDLIBS    = -lncursesw -ltinfo
endif

basic: basic.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f basic

.PHONY: clean
