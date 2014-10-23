/*
** tiff2png.c - converts a Tagged Image File to a Portable Network Graphics file
**
** Copyright (C) 1996 by Willem van Schaik, Singapore
**                       <gwillem@ntuvax.ntu.ac.sg>
**
** [see VERSION macro below for version and date]
**
** Lots of material was stolen from libtiff, tifftopnm, pnmtopng, which
** programs had also done a fair amount of "borrowing", so the credit for
** this program goes besides the author also to:
**         Sam Leffler
**         Jef Poskanzer
**         Alexander Lehmann
**         Patrick Naughton
**         Marcel Wijkstra
**
** usage(), pHYs support, error handler, updated libpng code, -faxpect option
**   by Greg Roelofs (newt@pobox.com), July/September 1999
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted,
** provided that the above copyright notice appear in all copies and that
** both that copyright notice and this permission notice appear in
** supporting documentation.
**
** This file is provided AS IS with no warranties of any kind.  The author
** shall have no liability with respect to the infringement of copyrights,
** trade secrets or any patents by this file or any part thereof.  In no
** event will the author be liable for any lost revenue or profits or
** other special, indirect and consequential damages.
*/

#define VERSION "0.7 of 16 September 1999"

#include <stdio.h>
#include <stdlib.h>

#include "tiff.h"
#include "tiffio.h"
#include "tiffcomp.h"
#include "png.h"

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#ifndef NONE
#  define NONE 0
#endif

#define MAXCOLORS 256

#ifndef PHOTOMETRIC_DEPTH
#  define PHOTOMETRIC_DEPTH 32768
#endif

/*
typedef	unsigned char u_char;
typedef	unsigned short u_short;
typedef	unsigned int u_int;
typedef	unsigned long u_long;
*/

typedef struct _jmpbuf_wrapper {
  jmp_buf jmpbuf;
} jmpbuf_wrapper;

static jmpbuf_wrapper tiff2png_jmpbuf_struct;

/* macros to get and put bits out of the bytes */

#define GET_LINE \
  { \
    if (bitsleft == 0) \
    { \
      p_line++; \
      bitsleft = 8; \
    } \
    bitsleft -= bps; \
    sample = (*p_line >> bitsleft) & maxval; \
  }
#define GET_STRIP \
  { \
    if (getbitsleft == 0) \
    { \
      p_strip++; \
      getbitsleft = 8; \
    } \
    getbitsleft -= bps; \
    sample = (*p_strip >> getbitsleft) & maxval; \
  }
#define PUT_LINE \
  { \
    if (putbitsleft == 0) \
    { \
      p_line++; \
      putbitsleft = 8; \
    } \
    putbitsleft -= bps; \
    *p_line |= ((sample & maxval) << putbitsleft); \
  }

/*----------------------------------------------------------------------------*/

static void usage (rc)
  int rc;
{
  char *pTIFFver;
  int len;

  fprintf (stderr, "Tiff2png version %s.\n", VERSION);

  /* this function returns a huge, three-line version+copyright string */
  pTIFFver = (char *)TIFFGetVersion();
  if (strncmp(pTIFFver, "LIBTIFF, Version ", 17) == 0)
  {
    char *p, *q;

    p = pTIFFver + 17;
    q = strchr(p, '\n');
    if (q && (len = (q-p)) < 15)   /* arbitrary (short) limit */
      fprintf (stderr, "   Compiled with libtiff %.*s.\n", len, p);
  }
  fprintf(stderr, "   Compiled with libpng %s; using libpng %s.\n",
    PNG_LIBPNG_VER_STRING, png_libpng_ver);
  fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n\n",
    ZLIB_VERSION, zlib_version);

  fprintf (stderr,
 "Usage: tiff2png [-verbose] [-gamma val] [-interlace] <tiff-file> <png-file>\n"
    );

  fprintf (stderr,
    "\nReads <tiff-file>, converts to PNG format, and writes <png-file>.\n");
  fprintf (stderr,
    "   -gamma option writes PNG with specified gamma value (e.g., 0.45455)\n");
  fprintf (stderr,
    "   -interlace option writes interlaced PNG\n");
#ifdef FAXPECT
  fprintf (stderr,
    "   -faxpect option converts 2:1 aspect-ratio faxes to square pixels\n");
#endif

  exit (rc);
}

/*----------------------------------------------------------------------------*/

static void tiff2png_error_handler (png_ptr, msg)
  png_structp png_ptr;
  png_const_charp msg;
{
  jmpbuf_wrapper  *jmpbuf_ptr;

  /* this function, aside from the extra step of retrieving the "error
   * pointer" (below) and the fact that it exists within the application
   * rather than within libpng, is essentially identical to libpng's
   * default error handler.  The second point is critical:  since both
   * setjmp() and longjmp() are called from the same code, they are
   * guaranteed to have compatible notions of how big a jmp_buf is,
   * regardless of whether _BSD_SOURCE or anything else has (or has not)
   * been defined. */

  fprintf(stderr, "Tiff2png:  fatal libpng error: %s\n", msg);
  fflush(stderr);

  jmpbuf_ptr = png_get_error_ptr(png_ptr);
  if (jmpbuf_ptr == NULL) {         /* we are completely hosed now */
    fprintf(stderr,
      "Tiff2png:  EXTREMELY fatal error: jmpbuf unrecoverable; terminating.\n");
    fflush(stderr);
    exit(99);
  }

  longjmp(jmpbuf_ptr->jmpbuf, 1);
}

/*----------------------------------------------------------------------------*/

int
main (argc, argv)
  int argc;
  char* argv[];

{
  int argn;
  int verbose = FALSE;
#ifdef FAXPECT
  int faxpect = FALSE;
#endif

  register TIFF* tif;
  char name [256];
  u_short bps, spp, planar;
  u_short photometric;
  int maxval;
  int colors;
  int halfcols, cols, rows;
  int row;
  register int col;
  u_char* tiffstrip;
  u_char* tiffline;
  register u_char *p_strip, *p_line;
  register u_char sample;
  long sample16;
  register int bitsleft;
  register int getbitsleft;
  register int putbitsleft;

  float xres, yres, ratio;			/* TIFF */
  png_uint_32 res_x_half, res_x, res_y;		/* PNG */
  int unit_type;
  int have_res = FALSE;

  unsigned short* redcolormap;
  unsigned short* greencolormap;
  unsigned short* bluecolormap;

  FILE *png;
  png_struct *png_ptr;
  png_info *info_ptr;
  png_byte *pngline;
  png_byte *p_png;
  png_color palette[MAXCOLORS];
  png_uint_32 width;
  float gamma = -1.0;
  int bit_depth = 0;
  int color_type = -1;
  int tiff_color_type;
  int interlace_type = PNG_INTERLACE_NONE;
  int pass;

  long i, j, b, n, s;

  /* debug */

#ifdef OLD_LIBTIFF
  if (verbose)
    fprintf (stderr, "Tiff2png: Old libtiff (like v2.4) is used\n");
#else
  if (verbose)
    fprintf (stderr, "Tiff2png: New libtiff (like v3.4) is used\n");
#endif

  /* get command-line arguments */

  argn = 1;
  verbose = 0;

  while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0')
  {
    if (strncmp (argv[argn], "-help", 2) == 0)
      usage (0);
    else if (strncmp (argv[argn], "-verbose", 2) == 0)
      verbose = TRUE;
    else if (strncmp (argv[argn], "-gamma", 2) == 0)
    {
      if (++argn < argc)
	sscanf (argv[argn], "%f", &gamma);
      else
	usage (1);
    }
    else if (strncmp (argv[argn], "-interlace", 2) == 0)
      interlace_type = PNG_INTERLACE_ADAM7;
#ifdef FAXPECT
    else if (strncmp (argv[argn], "-faxpect", 2) == 0)
      faxpect = TRUE;
#endif
    else
      usage (1);
    argn++;
  }

  if (argn != argc)
  {
    tif = TIFFOpen (argv[argn], "r");
    strcpy (name, argv[argn]);
    if (verbose)
      fprintf (stderr, "Tiff2png: %s\n", name);
    if (tif == NULL)
    {
      fprintf (stderr, "Error: TIFF file %s not found\n", argv[argn]);
      exit (1);
    }
    argn++;
  }
  else
    usage (1);

  if (argn != argc)
  {
    png = fopen (argv[argn], "wb");
    if (png == NULL)
    {
      fprintf (stderr, "Error: PNG file %s cannot be created\n", argv[argn]);
      exit (1);
    }
    argn++;
  }
  else
    usage (1);

  if (argn != argc)
    usage (1);

  /* get TIFF header info */

  if (verbose)
    TIFFPrintDirectory (tif, stderr, TIFFPRINT_NONE);

  if (! TIFFGetField (tif, TIFFTAG_PHOTOMETRIC, &photometric))
  {
    fprintf (stderr, "Error: photometric could not be retrieved\n");
    exit (1);
  }
  if (! TIFFGetField (tif, TIFFTAG_BITSPERSAMPLE, &bps))
    bps = 1;
  if (! TIFFGetField (tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
    spp = 1;
  if (! TIFFGetField (tif, TIFFTAG_PLANARCONFIG, &planar))
    planar = 1;

  (void) TIFFGetField (tif, TIFFTAG_IMAGEWIDTH, &cols);
  (void) TIFFGetField (tif, TIFFTAG_IMAGELENGTH, &rows);
  width = cols;

  if (TIFFGetField (tif, TIFFTAG_XRESOLUTION, &xres) &&
      TIFFGetField (tif, TIFFTAG_YRESOLUTION, &yres) &&  /* no default value */
      (xres != 0.0) && (yres != 0.0))
  {
    uint16 resunit;   /* typedef'd in tiff.h */

    have_res = TRUE;
    ratio = xres / yres;
    if (verbose)
    {
      fprintf (stderr, "Tiff2png: aspect ratio (hor/vert) = %g (%g / %g)\n",
        ratio, xres, yres);
      if (0.95 < ratio && ratio < 1.05)
        fprintf (stderr, "Tiff2png: near-unity aspect ratio\n");
      else if (1.90 < ratio && ratio < 2.10)
        fprintf (stderr, "Tiff2png: near-2X aspect ratio\n");
      else
        fprintf (stderr, "Tiff2png: non-square, non-2X pixels\n");
    }

#if 0
    /* GRR: this should be fine and works sometimes, but occasionally it
     *  seems to cause a segfault--which may be more related to Linux 2.2.10
     *  and/or SMP and/or heavy CPU loading.  Disabled for now. */
    (void) TIFFGetFieldDefaulted (tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
#else
    if (! TIFFGetField (tif, TIFFTAG_RESOLUTIONUNIT, &resunit))
      resunit = RESUNIT_INCH;  /* default (see libtiff tif_dir.c) */
#endif

    /* convert from TIFF data (floats) to PNG data (unsigned longs) */
    switch (resunit)
    {
      case RESUNIT_CENTIMETER:
        res_x_half = (png_uint_32)(50.0*xres + 0.5);
        res_x = (png_uint_32)(100.0*xres + 0.5);
        res_y = (png_uint_32)(100.0*yres + 0.5);
        unit_type = PNG_RESOLUTION_METER;
        break;
      case RESUNIT_INCH:
        res_x_half = (png_uint_32)(0.5*39.37*xres + 0.5);
        res_x = (png_uint_32)(39.37*xres + 0.5);
        res_y = (png_uint_32)(39.37*yres + 0.5);
        unit_type = PNG_RESOLUTION_METER;
        break;
/*    case RESUNIT_NONE:   */
      default:
        res_x_half = (png_uint_32)(50.0*xres + 0.5);
        res_x = (png_uint_32)(100.0*xres + 0.5);
        res_y = (png_uint_32)(100.0*yres + 0.5);
        unit_type = PNG_RESOLUTION_UNKNOWN;
        break;
    }
  }

#ifdef FAXPECT
  if (faxpect && (!have_res || ratio < 1.90 || ratio > 2.10))
  {
    fprintf (stderr,
      "Tiff2png: aspect ratio is out of range: skipping -faxpect conversion\n");
    faxpect = FALSE;
  }
#endif

  if (verbose)
  {
    fprintf (stderr, "Tiff2png: %dx%dx%d image\n", cols, rows, bps * spp);
    fprintf (stderr, "Tiff2png: %d bits/sample, %d samples/pixel\n", bps, spp);
  }

  /* start PNG preparation */

  png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
    &tiff2png_jmpbuf_struct, tiff2png_error_handler, NULL);
  if (!png_ptr)
  {
    fprintf (stderr, "Tiff2png error: cannot allocate libpng main struct\n");
    exit (4);
  }

  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr)
  {
    fprintf (stderr, "Tiff2png error: cannot allocate libpng info struct\n");
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    exit (4);
  }

  if (setjmp (tiff2png_jmpbuf_struct.jmpbuf))
  {
    fprintf (stderr, "Tiff2png error: setjmp returns error condition\n");
    png_destroy_write_struct (&png_ptr, &info_ptr);
    fclose (png);
    exit (1);
  }

  png_init_io (png_ptr, png);

  /* detect tiff filetype */

  maxval = (1 << bps) - 1;
  if (verbose)
    fprintf (stderr, "Tiff2png: maxval=%d\n", maxval);

  switch (photometric)
  {
    case PHOTOMETRIC_MINISBLACK:
    case PHOTOMETRIC_MINISWHITE:
      if (verbose)
	if (photometric == PHOTOMETRIC_MINISBLACK)
	  fprintf (stderr, "Tiff2png: %d graylevels (min=black)\n", maxval + 1);
	else
	  fprintf (stderr, "Tiff2png: %d graylevels (min=white)\n", maxval + 1);
      if (spp == 1) /* no alpha */
      {
	color_type = PNG_COLOR_TYPE_GRAY;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = grayscale\n");
	bit_depth = bps;
      }
      else /* must be alpha */
      {
	color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = grayscale + alpha\n");
	if (bps <= 8)
	  bit_depth = 8;
	else
	  bit_depth = bps;
      }
      break;

    case PHOTOMETRIC_PALETTE:
    {
      color_type = PNG_COLOR_TYPE_PALETTE;
      if (verbose)
	fprintf (stderr, "Tiff2png: color-type = paletted\n");

      if (! TIFFGetField (tif, TIFFTAG_COLORMAP, &redcolormap, &greencolormap,
          &bluecolormap))
      {
	fprintf (stderr, "Error: cannot retrieve TIFF colormaps\n");
	exit (1);
      }
      colors = maxval + 1;
      if (colors > MAXCOLORS)
      {
	fprintf (stderr, "Error: too large palette with %d colors\n", colors);
	exit (1);
      }
      /* max PNG palette-size is 8 bits, you could convert to full-color */
      if (bps >= 8) 
	bit_depth = 8;
      else
	bit_depth = bps;

      /* PLTE chunk */
      /* TIFF palettes contain 16-bit shorts, while PNG palettes are 8-bit) */
      for (i = 0 ; i < colors ; i++)
      {
	palette[i].red   = (png_byte) (redcolormap[i] >> 8);
	palette[i].green = (png_byte) (greencolormap[i] >> 8);
	palette[i].blue  = (png_byte) (bluecolormap[i] >> 8);
      }
      break;
    }

    case PHOTOMETRIC_RGB:
      if (spp == 3)
      {
	color_type = PNG_COLOR_TYPE_RGB;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = truecolor\n");
      }
      else
      {
	color_type = PNG_COLOR_TYPE_RGB_ALPHA;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = truecolor + alpha\n");
      }
      if (bps <= 8)
	bit_depth = 8;
      else
	bit_depth = bps;
      break;

    case PHOTOMETRIC_MASK:
    {
      fprintf (stderr, "Error: don't know how to handle PHOTOMETRIC_MASK\n");
      exit (1);
    }

    case PHOTOMETRIC_DEPTH:
    {
      fprintf (stderr, "Error: don't know how to handle PHOTOMETRIC_DEPTH\n");
      exit (1);
    }

    default:
    {
      fprintf (stderr, "Error: unknown photometric (%d)\n", photometric);
      exit (1);
    }
  }
  tiff_color_type = color_type;

  if (verbose)
    fprintf (stderr, "Tiff2png: bit-depth = %d\n", bit_depth);

#ifdef FAXPECT
  if (faxpect && (color_type != PNG_COLOR_TYPE_GRAY || bit_depth != 1))
  {
    fprintf (stderr,
      "Tiff2png: only B&W (1-bit grayscale) images supported for -faxpect\n");
    faxpect = FALSE;
  }
  /* reduce width of fax by 2X by converting 1-bit grayscale to 2-bit, 3-color
   * palette */
  if (faxpect)
  {
    width = halfcols = cols / 2;
    color_type = PNG_COLOR_TYPE_PALETTE;
    palette[0].red = palette[0].green = palette[0].blue = 0;	/* both 0 */
    palette[1].red = palette[1].green = palette[1].blue = 127;	/* 0,1 or 1,0 */
    palette[2].red = palette[2].green = palette[2].blue = 255;	/* both 1 */
    colors = 3;
    bit_depth = 2;
    res_x = res_x_half;
    if (verbose)
    {
      fprintf (stderr, "Tiff2png: new width = %lu pixels\n", width);
      fprintf (stderr, "Tiff2png: new color-type = paletted\n");
      fprintf (stderr, "Tiff2png: new bit-depth = %d\n", bit_depth);
    }
  }
#endif

  /* put parameter info in png-chunks */

  png_set_IHDR(png_ptr, info_ptr, width, rows, bit_depth, color_type,
    interlace_type, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_PLTE(png_ptr, info_ptr, palette, colors);

  /* gAMA chunk */
  if (gamma != -1.0)
  {
    if (verbose)
      fprintf (stderr, "Tiff2png: gamma = %f\n", gamma);
    png_set_gAMA(png_ptr, info_ptr, gamma);
  }

  /* pHYs chunk */
  if (have_res)
    png_set_pHYs (png_ptr, info_ptr, res_x, res_y, unit_type);

  png_write_info (png_ptr, info_ptr);
  png_set_packing (png_ptr);

  /* allocate spaces for one line of tiff-image */

  if (planar == 1) /* contiguous picture */
    tiffline = (u_char*) malloc(TIFFScanlineSize(tif));
  else /* separated planes */
    tiffline = (u_char*) malloc(TIFFScanlineSize(tif) * spp);

  if (tiffline == NULL)
  {
    fprintf (stderr, "Error: can't allocate memory for scanline buffer\n");
    exit (1);
  }
  if (planar != 1) /* in case we must combine more planes into one */
  {
    tiffstrip = (u_char*) malloc(TIFFScanlineSize(tif));
    if (tiffstrip == NULL)
    {
      fprintf (stderr, "Error: can't allocate memory for tiff strip buffer\n");
      exit (1);
    }
  }

  /* allocate space for one line of png-image */

  pngline = (u_char *) malloc (cols * 8); /* max: 3 color channels plus one alpha channel, 16 bit */

  for (pass = 0 ; pass < png_set_interlace_handling (png_ptr) ; pass++)
  {
    for (row = 0; row < rows; row++)
    {
      if (planar == 1) /* contiguous picture */
      {
	if (TIFFReadScanline (tif, tiffline, row, 0) < 0)
	{
	  fprintf (stderr, "Error: bad data read on line %d\n", row);
	  exit (1);
	}
      }
      else /* separated planes, then combine more strips into one line */
      {
        p_line = tiffline;
	for (n = 0; n < (cols/8 * bps*spp); n++)
	  *p_line++ = '\0';

	for (s = 0; s < spp; s++)
        {
          p_strip = tiffstrip;
          getbitsleft = 8;
          p_line = tiffline;
          putbitsleft = 8;

	  if (TIFFReadScanline(tif, tiffstrip, row, s) < 0)
	  {
	    fprintf (stderr, "Error: bad data read on line %d\n", row);
	    exit (1);
	  }

	  p_strip = (u_char *)tiffstrip;
	  sample = '\0';
          for (i = 0 ; i < s ; i++)
            PUT_LINE
	  for (n = 0; n < cols; n++)
	  {
	    GET_STRIP
	    PUT_LINE
	    sample = '\0';
	    for (i = 0 ; i < (spp-1) ; i++)
	      PUT_LINE
	  }
	} /* end for s */
      }

      p_line = tiffline;
      bitsleft = 8;
      p_png = pngline;

      /* convert from tiff-line to png-line */

      switch (tiff_color_type)
      {
        case PNG_COLOR_TYPE_GRAY:		/* we know spp == 1 */
	  for (col = cols; col > 0; --col)
	  {
            switch (bps)
	    {
              case 16:
#ifdef OLD_LIBTIFF
		if (photometric == PHOTOMETRIC_MINISWHITE)
		{
		  GET_LINE
		  sample16 = sample;
		  sample16 <<= 8;
		  GET_LINE
		  sample16 |= sample;
		  sample16 = maxval - sample16;
		  *p_png++ = (u_char)((sample16 >> 8) & 0x0F);
	  	  *p_png++ = (u_char)(sample16 & 0x0F);
		}
		else
#endif
		{
		  GET_LINE
		  *p_png++ = sample;
		  GET_LINE
		  *p_png++ = sample;
		}
		break;

              case 8:
              case 4:
              case 2:
              case 1:
		GET_LINE
#ifdef OLD_LIBTIFF
		if (photometric == PHOTOMETRIC_MINISWHITE)
		  sample = maxval - sample;
#endif
	        *p_png++ = sample;
		break;

            } /* end switch */
	  }
#ifdef FAXPECT
          /* note that this actually converts 1-bit grayscale to 2-bit indexed
           * data, where 0 = black, 1 = half-gray (127), and 2 = white */
          if (faxpect)
          {
	    png_byte *p_png2;

	    p_png = pngline;
	    p_png2 = pngline;
	    for (col = halfcols; col > 0; --col)
            {
	      *p_png++ = *p_png2 + p_png2[1];
	      p_png2 += 2;
            }
          }
#endif
	  break;

        case PNG_COLOR_TYPE_GRAY_ALPHA:
	  for (col = 0; col < cols; col++)
	  {
            for (i = 0 ; i < spp ; i++)
            {
              switch (bps)
	      {
                case 16:
#ifdef OLD_LIBTIFF
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		  {
		    GET_LINE
		    sample16 = sample;
		    sample16 <<= 8;
		    GET_LINE
		    sample16 |= sample;
		    sample16 = maxval - sample16;
		    *p_png++ = (u_char)((sample16 >> 8) & 0x0F);
	  	    *p_png++ = (u_char)(sample16 & 0x0F);
		  }
		  else
#endif
		  {
		    GET_LINE
		    *p_png++ = sample;
		    GET_LINE
		    *p_png++ = sample;
		  }
		  break;

                case 8:
		  GET_LINE
#ifdef OLD_LIBTIFF
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample;
		  break;

                case 4:
		  GET_LINE
#ifdef OLD_LIBTIFF
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample * 16;
		  break;

                case 2:
		  GET_LINE
#ifdef OLD_LIBTIFF
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample * 64;
		  break;

                case 1:
		  GET_LINE
#ifdef OLD_LIBTIFF
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample * 128;
		  break;

              } /* end switch */
            }
	  }
	  break;

        case PNG_COLOR_TYPE_RGB:
        case PNG_COLOR_TYPE_RGB_ALPHA:
	  for (col = 0; col < cols; col++)
	  {
            /* process for red, green and blue (and when applicable alpha) */
            for (i = 0 ; i < spp ; i++)
            {
              switch (bps)
	      {
                case 16:
		  GET_LINE
		  *p_png++ = sample;
		  GET_LINE
		  *p_png++ = sample;
		  break;

                case 8:
		  GET_LINE
	          *p_png++ = sample;
		  break;

                /* GRR:  THESE THREE CASES CANNOT HAPPEN: */

                case 4:
		  GET_LINE
	          *p_png++ = sample * 16;
		  break;

                case 2:
		  GET_LINE
	          *p_png++ = sample * 64;
		  break;

                case 1:
		  GET_LINE
	          *p_png++ = sample * 128;
		  break;

              } /* end switch */
		
            }
	  }
	  break;
  
        case PNG_COLOR_TYPE_PALETTE:
	  for (col = 0; col < cols; col++)
	  {
	    GET_LINE
            *p_png++ = sample;
	  }
	  break;
  
	default:
	{
	  fprintf (stderr, "Error: unknown photometric (%d)\n", photometric);
	  exit (1);
	}

      }
      png_write_row (png_ptr, pngline);
    }
  } /* for pass */

  png_write_end (png_ptr, info_ptr);
  fclose (png);

  png_destroy_write_struct (&png_ptr, &info_ptr);

  exit (0);
}
