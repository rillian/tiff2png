# Makefile for tiff2png
# Copyright 1996 Willem van Schaik
# Copyright 2002 Greg Roelofs
# Copyright 2014 Ralph Giles

CC ?= gcc
CFLAGS ?= -g -Wall -O3
COPY = /bin/cp -p

# It appears that PHOTOMETRIC_MINISWHITE should always be inverted (which
# makes sense), but if you find a class of TIFFs or a version of libtiff for
# which that is *not* the case, try not defining INVERT_MINISWHITE.
#
# FAXPECT is a custom conversion option for 2:1 stretched faxes. [GRR 19990713]
#
# DEFAULT_DESTDIR_IS_CURDIR will put all converted images into the current
# directory (.) if the -destdir option is given without an argument.
#
OPTION_FLAGS = -DINVERT_MINISWHITE -DFAXPECT -DDEFAULT_DESTDIR_IS_CURDIR

PREFIX = /usr/local

CFLAGS += $(TIFF_VERSION) $(OPTION_FLAGS)
LIBS = -ltiff -ljpeg -lpng -lz -lm

all: tiff2png

SRCS = tiff2png.c
OBJS = $(SRCS:%.c=%.o)

tiff2png: tiff2png.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(OBJS) tiff2png

install: all
	$(COPY) tiff2png $(INSTALL)/bin/

.PHONY: all clean install
