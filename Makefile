# Makefile for tiff2png
# Copyright (C) 1996 Willem van Schaik

MODEL=- 
CFLAGS=-Ox -GA3s -nologo -W3 -I..\zlib

CC=cl
CLFLAGS=/nologo /MD /GX /O2 /DWIN32 /DNDEBUG /D_WINDOWS /c
LINK32=link.exe
LINKFLAGS=/nologo /machine:I386 /incremental:no

O=.obj

#CC=cc
#CC=gcc
#COPY=cp
#COPY=/bin/cp -p
COPY=copy
#DEL=/bin/rm -f
DEL=del /f 

# change to match your directories (you see the ./ and ../ ?!?!)
#LIBTIFF=./libtiff
# LIBTIFF=/work/graphics/libgr2/tiff/libtiff
#LIBTIFF=../netpbm/libtiff
# newer libtiffs (can) use libjpeg, too
#LIBJPEG=/work/graphics/libgr2/jpeg
LIBTIFF=..\..\libtiff

LIBPNG=../libpng
#LIBPNG=/usr/lib
ZLIB=..\zlib
#ZLIB=/usr/lib

#INSTALL=/usr/local
INSTALL=..\exe

# GRR 19990713:  FAXPECT is a custom conversion option for stretched faxes
CFLAGS= \
	-DFAXPECT \
	-I. \
	-I$(LIBTIFF)\inc \
	-I$(LIBPNG) \
	-I$(ZLIB)
	
SLDFLAGS= \
	$(LIBTIFF)\lib\libtiff.lib \
	$(LIBPNG)\libpng.lib \
	$(ZLIB)\zlib.lib

OBJS = tiff2png.obj

all: tiff2png.exe

tiff2png.exe : $(OBJS)
    $(LINK32) @<<
    $(LINKFLAGS) $(OBJS) $(SLDFLAGS)
<<

tiff2png.obj : tiff2png.c
        cl $(CLFLAGS) $(CFLAGS) tiff2png.c

clean:
	$(DEL) *.obj tiff2png

# leave this line empty

$(OBJS):
