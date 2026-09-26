# A program built from the gates package alone (plan-0015, T040/T042).
# This folder sits in the package at examples/consumer/, so the package root is
# ../.. - point PKG elsewhere for an installed copy: make host PKG=/some/prefix
#   make host   headless.c against lib/host (gcc or clang, any system)
#   make win    main.c against lib/win64 (mingw-w64 cross compiler or MSYS2)
PKG    ?= ../..
CFLAGS ?= -std=c23 -Wall -Wextra -Werror -O2
CC_WIN ?= x86_64-w64-mingw32-gcc
WIN_LIBS = -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 \
           -luiautomationcore -lole32 -loleaut32 -luuid

host: headless
win: consumer.exe

headless: headless.c
	$(CC) $(CFLAGS) -I$(PKG)/include $< -L$(PKG)/lib/host -lgates_core -lproven -lm -o $@

consumer.exe: main.c
	$(CC_WIN) $(CFLAGS) -I$(PKG)/include $< -L$(PKG)/lib/win64 -lgates -lproven $(WIN_LIBS) -mwindows -o $@

clean:
	rm -f headless consumer.exe

.PHONY: host win clean
