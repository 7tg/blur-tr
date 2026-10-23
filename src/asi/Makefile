# Cross-build blur_tr.asi (a 32-bit Windows DLL) under MinGW-w64.
#
# Target: PC Blur (2010), loaded by Ultimate ASI Loader.  Must be i386.

CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -Wall -Wextra -municode
# Target the ancient msvcrt (not UCRT): Wine always ships it, and it keeps
# this ASI free of `api-ms-win-crt-*` forwarder imports that may be missing
# on bare Wine installs.
LDFLAGS := -shared -static-libgcc -Wl,--kill-at -nodefaultlibs
LIBS    := -lmingw32 -lgcc -lmoldname -lmingwex -lmsvcrt -lkernel32 -luser32

OUT := blur_tr.asi
SRC := blur_tr.c

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
	i686-w64-mingw32-strip --strip-unneeded $@

clean:
	rm -f $(OUT)

.PHONY: clean
