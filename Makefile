TARGET = pixel_fix
OBJS = main.o exports.o

USE_PSPSDK_LIBC = 1

CFLAGS = -O2 -G0 -Wall
LDFLAGS = -u sceDisplaySetFrameBuf -u sceDisplayGetFrameBuf

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
