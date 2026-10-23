TARGET = RANDOM
OBJS = src/main.o

INCDIR =
CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

LIBS = -lpspdebug -lpspdisplay -lpspctrl -lpspgu -lpspgum

BUILD_PRX = 0
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = RANDOM

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
