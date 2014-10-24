/*
** tiff2png.c - converts Tagged Image File Format to Portable Network Graphics
**
** Copyright 1996,2000 Willem van Schaik, Calgary (willem@schaik.com)
** Copyright 1999-2002 Greg Roelofs (newt@pobox.com)
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
** GRR REVISION HISTORY
** ====================
**
** 19990713, 19990916 [version 0.7]:
**    added usage(), pHYs support, error handler, -faxpect option; updated
**    libpng code
**
** 20000126 [version 0.8]:
**    added multi-file support, -destdir and -compression options; improved
**    error-handling
**
** 20000213 [version 0.81]:
**    changed -destdir to default to current directory (TenThumbs/SJT); fixed
**    gcc warning
**
** 20001104 [version 0.81b]:
**    incorporated Willem's 16-bit fix (GET_LINE, GET_STRIP, PUT_LINE macros)
**
** 20001112 [version 0.81c]:
**    fixed (most of) rest of 16-bit bug (byteorder, 0xff); incorporated
**    -invert option; added YCbCr, Log(L) support; added Win32 (MSVC) support
**
** 20020701 [version 0.9]:
**    added support for contiguous tiled TIFFs (Frank Palmieri); added EMX
**    wildcard support; added check/fix for (invalid?) 8-bit palette data in
**    16-bit palettes (TenThumbs); removed C++ comments (Nelson Beebe); added
**    more checks for malloc() failure
**
** 20020912 [version 0.91]:
**    fixed sub-8-bps RGB[A] scaling bugs; fixed (and verified!--thanks to
**    Rhett Sutphin) all(?) remaining 16-bps bugs
**
**    NOTE:  libtiff always returns 16-bit or larger samples in the native
**           machine order when reading a TIFF, regardless of the TIFF image's
**           actual endianness.  On x86 this means the samples are always
**           little-endian and must be swapped for PNG; on PowerPC and 68k,
**           no extra work is required.  There does not appear to be any way
**           to alter this when reading TIFFs, although there are some flags
**           to TIFFOpen() that support variations when _writing_ them.
**
** To do:  add testing/support for associated vs. unassociated alpha channel
**         add support for iCCP profiles (and autodetect sRGB?)
**       / add support for text annotations
**       \ incorporate Willem's remaining 0.82 changes
**         check various "XXX" items (non-contiguous tiles? MINISWHITE RGB? ...)
**         create a man page
**         [maybe switch to equivalent (OSS Certified) libpng or zlib license?]
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

#define VERSION "0.91 of 12 September 2002"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiff.h"
#include "tiffio.h"
#include "png.h"

#include "zlib.h"

#ifdef _MSC_VER   /* works for MSVC 5.0; need finer tuning? */
#  define strcasecmp _stricmp
#endif

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

#define DIR_SEP '/'		/* SJT: Unix-specific */

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

typedef struct _jmpbuf_wrapper {
  jmp_buf jmpbuf;
} jmpbuf_wrapper;

static jmpbuf_wrapper tiff2png_jmpbuf_struct;


/* local prototypes */

static void usage (int rc);
static void tiff2png_error_handler (png_structp png_ptr, png_const_charp msg);
int tiff2png (char *tiffname, char *pngname, int verbose, int force,
              int interlace_type, int png_compression_level, int invert,
              int faxpect_option,
              double gamma);


/* macros to get and put bits out of the bytes */

#define GET_LINE_SAMPLE \
  { \
    if (bitsleft == 0) \
    { \
      p_line++; \
      bitsleft = 8; \
    } \
    bitsleft -= (bps >= 8) ? 8 : bps; \
    sample = (*p_line >> bitsleft) & maxval; \
    if (invert) \
      sample = ~sample & maxval; \
  }
#define GET_STRIP_SAMPLE \
  { \
    if (getbitsleft == 0) \
    { \
      p_strip++; \
      getbitsleft = 8; \
    } \
    getbitsleft -= (bps >= 8) ? 8 : bps; \
    sample = (*p_strip >> getbitsleft) & maxval; \
    if (invert) \
      sample = ~sample & maxval; \
  }
#define PUT_LINE_SAMPLE \
  { \
    if (putbitsleft == 0) \
    { \
      p_line++; \
      putbitsleft = 8; \
    } \
    putbitsleft -= (bps >= 8) ? 8 : bps; \
    if (invert) \
      sample = ~sample; \
    *p_line |= ((sample & maxval) << putbitsleft); \
  }

/*----------------------------------------------------------------------------*/

static void usage (rc)
  int rc;
{
  char *pTIFFver;
  int len;

  fprintf (stderr,
    "tiff2png version %s (Willem van Schaik and Greg Roelofs)\n", VERSION);

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
    "Usage:  tiff2png [-verbose] [-force] [-destdir <dir>] [-compression <val>]"
    "\n                 [-gamma <val>] [-interlace] [-invert] "
    "[-faxpect] "
    "<file> [...]\n\n"
    "Read each <file> and convert to PNG format (by default, in same directory "
    "as\n"
    "corresponding TIFF).  Suffixes will be changed from .tif or .tiff to .png."
    "\n\n"
    "   -force        overwrite existing PNGs if they exist\n"
    "   -destdir      create PNGs in destination directory <dir>\n"
    "   -compression  set the zlib compression level to <val> (0-9)\n"
    "   -gamma        write PNGs with specified gamma <val> (e.g., 0.45455)\n"
    "   -interlace    write interlaced PNGs\n"
    "   -invert       invert grayscale images (swaps black/white)\n");
  fprintf (stderr,
    "   -faxpect      convert fax with 2:1 aspect ratio to square pixels\n");

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

  fprintf (stderr, "tiff2png:  fatal libpng error: %s\n", msg);
  fflush (stderr);

  jmpbuf_ptr = png_get_error_ptr (png_ptr);
  if (jmpbuf_ptr == NULL) {         /* we are completely hosed now */
    fprintf (stderr,
      "tiff2png:  EXTREMELY fatal error: jmpbuf unrecoverable; terminating.\n");
    fflush (stderr);
    exit (99);
  }

  longjmp (jmpbuf_ptr->jmpbuf, 1);
}

/*----------------------------------------------------------------------------*/

int
tiff2png (tiffname, pngname, verbose, force, interlace_type,
          png_compression_level, _invert, faxpect_option, gamma)
  char *tiffname, *pngname;
  int verbose, force, interlace_type, png_compression_level, _invert;
  int faxpect_option;
  double gamma;
{
  static TIFF *tif = NULL;				/* TIFF */
  ush bps, spp, planar;
  ush photometric, tiff_compression_method;
  int bigendian;
  int maxval;
  static int colors = 0;
  static int halfcols = 0;
  int cols, rows;
  int row;
  register int col;
  static uch *tiffstrip = NULL;
  uch *tiffline;

  size_t stripsz;
  static size_t tilesz = 0L;
  uch *tifftile; /* FAP 20020610 - Add variables to support tiled images */
  ush tiled;
  uint32 tile_width, tile_height;   /* typedef'd in tiff.h */
  static int num_tilesX = 0;

  register uch *p_strip, *p_line;
  register uch sample;
#ifdef INVERT_MINISWHITE
  int sample16;
#endif
  register int bitsleft;
  register int getbitsleft;
  register int putbitsleft;
  float xres, yres, ratio;
#ifdef GRR_16BIT_DEBUG
  uch msb_max, lsb_max;
  uch msb_min, lsb_min;
  int s16_max, s16_min;
#endif

  static FILE *png = NULL;				/* PNG */
  png_struct *png_ptr;
  png_info *info_ptr;
  png_byte *pngline;
  png_byte *p_png;
  png_color palette[MAXCOLORS];
  static png_uint_32 width;
  int bit_depth = 0;
  int color_type = -1;
  int tiff_color_type;
  int pass;
  static png_uint_32 res_x_half=0L, res_x=0L, res_y=0L;
  static int unit_type = 0;

  unsigned short *redcolormap;
  unsigned short *greencolormap;
  unsigned short *bluecolormap;
  static int have_res = FALSE;
  static int invert;
  int faxpect;
  long i, n;


  /* first figure out whether this machine is big- or little-endian */
  {
    union { int32 i; char c[4]; } endian_tester;

    endian_tester.i = 1;
    bigendian = (endian_tester.c[0] == 0);
  }

/*
  tilesz = 0L;
  num_tilesX = 0;
 */
  invert = _invert;

  tif = TIFFOpen (tiffname, "r");
  if (tif == NULL)
  {
    fprintf (stderr, "tiff2png error:  TIFF file %s not found\n", tiffname);
    return 1;
  }

  if (!force)
  {
    png = fopen (pngname, "rb");
    if (png)
    {
      fprintf (stderr, "tiff2png warning:  PNG file %s exists: skipping\n",
        pngname);
      fclose (png);
      TIFFClose (tif);
      return 1;
    }
  }

  png = fopen (pngname, "wb");
  if (png == NULL)
  {
    fprintf (stderr, "tiff2png error:  PNG file %s cannot be created\n",
      pngname);
    TIFFClose (tif);
    return 1;
  }

  if (verbose)
    fprintf (stderr, "\ntiff2png:  converting %s to %s\n", tiffname, pngname);


  /* start PNG preparation */

  png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
    &tiff2png_jmpbuf_struct, tiff2png_error_handler, NULL);
  if (!png_ptr)
  {
    fprintf (stderr,
      "tiff2png error:  cannot allocate libpng main struct (%s)\n", pngname);
    TIFFClose (tif);
    fclose (png);
    return 4;
  }

  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr)
  {
    fprintf (stderr,
      "tiff2png error:  cannot allocate libpng info struct (%s)\n", pngname);
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    TIFFClose (tif);
    fclose (png);
    return 4;
  }

  if (setjmp (tiff2png_jmpbuf_struct.jmpbuf))
  {
    fprintf (stderr, "tiff2png error:  libpng returns error condition (%s)\n",
      pngname);
    png_destroy_write_struct (&png_ptr, &info_ptr);
    TIFFClose (tif);
    fclose (png);
    return 1;
  }

  png_init_io (png_ptr, png);


  /* get TIFF header info */

  if (verbose)
  {
    int byteswapped = TIFFIsByteSwapped(tif);   /* why no TIFFIsBigEndian()?? */

    fprintf (stderr, "tiff2png:  ");
    TIFFPrintDirectory (tif, stderr, TIFFPRINT_NONE);
    fprintf (stderr, "tiff2png:  byte order = %s\n",
      ((bigendian && byteswapped) || (!bigendian && !byteswapped))?
      "little-endian (Intel)" : "big-endian (Motorola)");
    fprintf (stderr, "tiff2png:  this machine is %s-endian\n",
      bigendian? "big" : "little");
  }

  if (! TIFFGetField (tif, TIFFTAG_PHOTOMETRIC, &photometric))
  {
    fprintf (stderr,
      "tiff2png error:  photometric could not be retrieved (%s)\n", tiffname);
    png_destroy_write_struct (&png_ptr, &info_ptr);
    TIFFClose (tif);
    fclose (png);
    return 1;
  }
  if (! TIFFGetField (tif, TIFFTAG_BITSPERSAMPLE, &bps))
    bps = 1;
  if (! TIFFGetField (tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
    spp = 1;
  if (! TIFFGetField (tif, TIFFTAG_PLANARCONFIG, &planar))
    planar = 1;

  tiled = TIFFIsTiled(tif); /* FAP 20020610 - get tiled flag */

  (void) TIFFGetField (tif, TIFFTAG_IMAGEWIDTH, &cols);
  (void) TIFFGetField (tif, TIFFTAG_IMAGELENGTH, &rows);
  width = cols;

  ratio = 0.0;

  if (TIFFGetField (tif, TIFFTAG_XRESOLUTION, &xres) &&
      TIFFGetField (tif, TIFFTAG_YRESOLUTION, &yres) &&  /* no default value */
      (xres != 0.0) && (yres != 0.0))
  {
    uint16 resunit;   /* typedef'd in tiff.h */

    have_res = TRUE;
    ratio = xres / yres;
    if (verbose)
    {
      fprintf (stderr, "tiff2png:  aspect ratio (hor/vert) = %g (%g / %g)\n",
        ratio, xres, yres);
      if (0.95 < ratio && ratio < 1.05)
        fprintf (stderr, "tiff2png:  near-unity aspect ratio\n");
      else if (1.90 < ratio && ratio < 2.10)
        fprintf (stderr, "tiff2png:  near-2X aspect ratio\n");
      else
        fprintf (stderr, "tiff2png:  non-square, non-2X pixels\n");
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

  if (verbose)
  {
    fprintf (stderr, "tiff2png:  %dx%dx%d image\n", cols, rows, bps * spp);
    fprintf (stderr, "tiff2png:  %d bit%s/sample, %d sample%s/pixel\n",
      bps, bps == 1? "" : "s", spp, spp == 1? "" : "s");
  }

  /* detect tiff filetype */

  maxval = (1 << bps) - 1;
  if (verbose)
    fprintf (stderr, "tiff2png:  maxval=%d\n", maxval);

  switch (photometric)
  {
    case PHOTOMETRIC_MINISWHITE:
    case PHOTOMETRIC_MINISBLACK:
      if (verbose)
	fprintf (stderr, "tiff2png:  %d graylevels (min = %s)\n", maxval + 1,
	  photometric == PHOTOMETRIC_MINISBLACK? "black" : "white");
      if (spp == 1) /* no alpha */
      {
	color_type = PNG_COLOR_TYPE_GRAY;
	if (verbose)
	  fprintf (stderr, "tiff2png:  color type = grayscale\n");
	bit_depth = bps;
      }
      else /* must be alpha */
      {
	color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
	if (verbose)
	  fprintf (stderr, "tiff2png:  color type = grayscale + alpha\n");
	if (bps <= 8)
	  bit_depth = 8;
	else
	  bit_depth = bps;
      }
      break;

    case PHOTOMETRIC_PALETTE:
    {
      int palette_8bit; /* set iff all color values in TIFF palette are < 256 */

      color_type = PNG_COLOR_TYPE_PALETTE;
      if (verbose)
	fprintf (stderr, "tiff2png:  color type = paletted\n");

      if (! TIFFGetField (tif, TIFFTAG_COLORMAP, &redcolormap, &greencolormap,
          &bluecolormap))
      {
	fprintf (stderr,
          "tiff2png error:  cannot retrieve TIFF colormaps (%s)\n", tiffname);
        png_destroy_write_struct (&png_ptr, &info_ptr);
        TIFFClose (tif);
        fclose (png);
	return 1;
      }
      colors = maxval + 1;
      if (colors > MAXCOLORS)
      {
	fprintf (stderr,
          "tiff2png error:  palette too large (%d colors) (%s)\n", colors,
          tiffname);
        png_destroy_write_struct (&png_ptr, &info_ptr);
        TIFFClose (tif);
        fclose (png);
	return 1;
      }
      /* max PNG palette-size is 8 bits, you could convert to full-color */
      if (bps >= 8) 
	bit_depth = 8;
      else
	bit_depth = bps;

      /* PLTE chunk */
      /* TIFF palettes contain 16-bit shorts, while PNG palettes are 8-bit */
      /* Some broken (??) software puts 8-bit values in the shorts, which would
         make the palette come out all zeros, which isn't good. We check... */
      palette_8bit = 1;
      for (i = 0 ; i < colors ; i++)
      {
        if ( redcolormap[i] > 255   || 
             greencolormap[i] > 255 ||
             bluecolormap[i] > 255)
        {
           palette_8bit = 0;
           break;
        }
      }	
      if (palette_8bit && verbose)
        fprintf(stderr, "tiff2png warning:  assuming 8-bit palette values\n");

      for (i = 0 ; i < colors ; i++)
      {
        if (invert)
        {
          if (palette_8bit)
          {
            palette[i].red   = ~((png_byte) redcolormap[i]);
            palette[i].green = ~((png_byte) greencolormap[i]);
            palette[i].blue  = ~((png_byte) bluecolormap[i]);
          }
          else
          {
            palette[i].red   = ~((png_byte) (redcolormap[i] >> 8));
            palette[i].green = ~((png_byte) (greencolormap[i] >> 8));
            palette[i].blue  = ~((png_byte) (bluecolormap[i] >> 8));
          }
        }
        else
        {
          if (palette_8bit)
          {
	    palette[i].red   = (png_byte) redcolormap[i];
	    palette[i].green = (png_byte) greencolormap[i];
	    palette[i].blue  = (png_byte) bluecolormap[i];
          }
          else
          {
	    palette[i].red   = (png_byte) (redcolormap[i] >> 8);
	    palette[i].green = (png_byte) (greencolormap[i] >> 8);
	    palette[i].blue  = (png_byte) (bluecolormap[i] >> 8);
          }
        }
      }
      /* prevent index data (pixel values) from being inverted (-> garbage) */
      invert = FALSE;
      break;
    }

    case PHOTOMETRIC_YCBCR:
      /* GRR 20001110:  lifted from tiff2ps in libtiff 3.5.4 */
      TIFFGetField(tif, TIFFTAG_COMPRESSION, &tiff_compression_method);
      if (tiff_compression_method == COMPRESSION_JPEG &&
          planar == PLANARCONFIG_CONTIG)
      {
        /* can rely on libjpeg to convert to RGB */
        TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
        photometric = PHOTOMETRIC_RGB;
        if (verbose)
          fprintf (stderr,
            "tiff2png:  original color type = YCbCr with JPEG compression\n");
      }
      else
      {
        fprintf (stderr,
          "tiff2png error:  don't know how to handle PHOTOMETRIC_YCBCR with\n"
          "  compression %d (%sJPEG) and planar config %d (%scontiguous)\n"
          "  (%s)\n", tiff_compression_method,
          tiff_compression_method == COMPRESSION_JPEG? "" : "not ",
          planar, planar == PLANARCONFIG_CONTIG? "" : "not ", tiffname);
        png_destroy_write_struct (&png_ptr, &info_ptr);
        TIFFClose (tif);
        fclose (png);
        return 1;
      }
      /* fall thru... */

    case PHOTOMETRIC_RGB:
      if (spp == 3)
      {
	color_type = PNG_COLOR_TYPE_RGB;
	if (verbose)
	  fprintf (stderr, "tiff2png:  color type = truecolor\n");
      }
      else
      {
	color_type = PNG_COLOR_TYPE_RGB_ALPHA;
	if (verbose)
	  fprintf (stderr, "tiff2png:  color type = truecolor + alpha\n");
      }
      if (bps <= 8)
	bit_depth = 8;
      else
	bit_depth = bps;
      break;

    case PHOTOMETRIC_LOGL:
    case PHOTOMETRIC_LOGLUV:
      /* GRR 20001110:  lifted from tiff2ps from libtiff 3.5.4 */
      TIFFGetField(tif, TIFFTAG_COMPRESSION, &tiff_compression_method);
      if (tiff_compression_method != COMPRESSION_SGILOG &&
          tiff_compression_method != COMPRESSION_SGILOG24)
      {
        fprintf (stderr,
          "tiff2png error:  don't know how to handle PHOTOMETRIC_LOGL%s with\n"
          "  compression %d (not SGILOG) (%s)\n",
          photometric == PHOTOMETRIC_LOGLUV? "UV" : "",
          tiff_compression_method, tiffname);
        png_destroy_write_struct (&png_ptr, &info_ptr);
        TIFFClose (tif);
        fclose (png);
        return 1;
      }
      /* rely on library to convert to RGB/greyscale */
#ifdef LIBTIFF_HAS_16BIT_INTEGER_FORMAT
      if (bps > 8)
      {
        /* SGILOGDATAFMT_16BIT converts to a floating-point luminance value;
         *  U,V are left as such.  SGILOGDATAFMT_16BIT_INT doesn't exist. */
        TIFFSetField(tif, TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_16BIT_INT);
        bit_depth = bps = 16;
      }
      else
#endif
      {
        /* SGILOGDATAFMT_8BIT converts to normal grayscale or RGB format */
        TIFFSetField(tif, TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_8BIT);
        bit_depth = bps = 8;
      }
      if (photometric == PHOTOMETRIC_LOGL)
      {
        photometric = PHOTOMETRIC_MINISBLACK;
        color_type = PNG_COLOR_TYPE_GRAY;
        if (verbose)
        {
          fprintf (stderr,
            "tiff2png:  original color type = logL with SGILOG compression\n");
          fprintf (stderr, "tiff2png:  color type = grayscale\n");
        }
      }
      else
      {
        photometric = PHOTOMETRIC_RGB;
        color_type = PNG_COLOR_TYPE_RGB;
        if (verbose)
        {
          fprintf (stderr,
           "tiff2png:  original color type = logLUV with SGILOG compression\n");
          fprintf (stderr, "tiff2png:  color type = truecolor\n");
        }
      }
      break;

/*
    case PHOTOMETRIC_YCBCR:
    case PHOTOMETRIC_LOGL:
    case PHOTOMETRIC_LOGLUV:
 */
    case PHOTOMETRIC_MASK:
    case PHOTOMETRIC_SEPARATED:
    case PHOTOMETRIC_CIELAB:
    case PHOTOMETRIC_DEPTH:
    {
      fprintf (stderr,
        "tiff2png error:  don't know how to handle %s (%s)\n",
/*
        photometric == PHOTOMETRIC_YCBCR?     "PHOTOMETRIC_YCBCR" :
        photometric == PHOTOMETRIC_LOGL?      "PHOTOMETRIC_LOGL" :
        photometric == PHOTOMETRIC_LOGLUV?    "PHOTOMETRIC_LOGLUV" :
 */
        photometric == PHOTOMETRIC_MASK?      "PHOTOMETRIC_MASK" :
        photometric == PHOTOMETRIC_SEPARATED? "PHOTOMETRIC_SEPARATED" :
        photometric == PHOTOMETRIC_CIELAB?    "PHOTOMETRIC_CIELAB" :
        photometric == PHOTOMETRIC_DEPTH?     "PHOTOMETRIC_DEPTH" :
                                              "unknown photometric",
        tiffname);
      png_destroy_write_struct (&png_ptr, &info_ptr);
      TIFFClose (tif);
      fclose (png);
      return 1;
    }

    default:
    {
      fprintf (stderr, "tiff2png error:  unknown photometric (%d) (%s)\n",
        photometric, tiffname);
      png_destroy_write_struct (&png_ptr, &info_ptr);
      TIFFClose (tif);
      fclose (png);
      return 1;
    }
  }
  tiff_color_type = color_type;

  if (verbose)
    fprintf (stderr, "tiff2png:  bit depth = %d\n", bit_depth);

  faxpect = faxpect_option;
  if (faxpect && (!have_res || ratio < 1.90 || ratio > 2.10))
  {
    fprintf (stderr,
     "tiff2png:  aspect ratio is out of range: skipping -faxpect conversion\n");
    faxpect = FALSE;
  }

  if (faxpect && (color_type != PNG_COLOR_TYPE_GRAY || bit_depth != 1))
  {
    fprintf (stderr,
      "tiff2png:  only B&W (1-bit grayscale) images supported for -faxpect\n");
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
      fprintf (stderr, "tiff2png:  new width = %u pixels\n", width);
      fprintf (stderr, "tiff2png:  new color type = paletted\n");
      fprintf (stderr, "tiff2png:  new bit depth = %d\n", bit_depth);
    }
  }

  /* put parameter info in png-chunks */

  png_set_IHDR(png_ptr, info_ptr, width, rows, bit_depth, color_type,
    interlace_type, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  if (png_compression_level != -1)
    png_set_compression_level(png_ptr, png_compression_level);

  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_PLTE(png_ptr, info_ptr, palette, colors);

  /* gAMA chunk */
  if (gamma != -1.0)
  {
    if (verbose)
      fprintf (stderr, "tiff2png:  gamma = %f\n", gamma);
    png_set_gAMA(png_ptr, info_ptr, gamma);
  }

  /* pHYs chunk */
  if (have_res)
    png_set_pHYs (png_ptr, info_ptr, res_x, res_y, unit_type);

  png_write_info (png_ptr, info_ptr);
  png_set_packing (png_ptr);


  /* allocate space for one line (or row of tiles) of TIFF image */

  tiffline = NULL;
  tifftile = NULL;
  tiffstrip = NULL;

  if (!tiled)      /* strip-based TIFF */
  {
    if (planar == 1) /* contiguous picture */
      tiffline = (uch*) malloc(TIFFScanlineSize(tif));
    else /* separated planes */
      tiffline = (uch*) malloc(TIFFScanlineSize(tif) * spp);
  }
  else
  {
    /* FAP 20020610 - tiled support - allocate space for one "row" of tiles */

    TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
    TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);

    num_tilesX = (width+tile_width-1)/tile_width;

    if (planar == 1)
    {
      tilesz = TIFFTileSize(tif);
      tifftile = (uch*) malloc(tilesz);
      if (tifftile == NULL)
      {
        fprintf (stderr,
          "tiff2png error:  can't allocate memory for TIFF tile buffer (%s)\n",
          tiffname);
        png_destroy_write_struct (&png_ptr, &info_ptr);
        TIFFClose (tif);
        fclose (png);
        return 4;
      }
      stripsz = (tile_width*num_tilesX) * tile_height * spp;
      tiffstrip = (uch*) malloc(stripsz);
      tiffline = tiffstrip; /* just set the line to the top of the strip.
                             * we'll move it through below. */
    }
    else
    {
      fprintf (stderr,
        "tiff2png error: can't handle tiled separated-plane TIFF format (%s)\n",
        tiffname);
      png_destroy_write_struct (&png_ptr, &info_ptr);
      TIFFClose (tif);
      fclose (png);
      return 5;
    }
  }

  if (tiffline == NULL)
  {
    fprintf (stderr,
      "tiff2png error:  can't allocate memory for TIFF scanline buffer (%s)\n",
      tiffname);
    png_destroy_write_struct (&png_ptr, &info_ptr);
    TIFFClose (tif);
    fclose (png);
    if (tiled && planar == 1)
      free(tifftile);
    return 4;
  }

  if (planar != 1) /* in case we must combine more planes into one */
  {
    tiffstrip = (uch*) malloc(TIFFScanlineSize(tif));
    if (tiffstrip == NULL)
    {
      fprintf (stderr,
        "tiff2png error:  can't allocate memory for TIFF strip buffer (%s)\n",
        tiffname);
      png_destroy_write_struct (&png_ptr, &info_ptr);
      TIFFClose (tif);
      free(tiffline);
      fclose (png);
      return 4;
    }
  }


  /* allocate space for one line of PNG image */
  /* max: 3 color channels plus one alpha channel, 16 bit => 8 bytes/pixel */

  pngline = (uch *) malloc (cols * 8);
  if (pngline == NULL)
  {
    fprintf (stderr,
      "tiff2png error:  can't allocate memory for PNG row buffer (%s)\n",
      tiffname);
    png_destroy_write_struct (&png_ptr, &info_ptr);
    TIFFClose (tif);
    free(tiffline);
    if (tiled && planar == 1)
      free(tifftile);
    else if (planar != 1)
      free(tiffstrip);
    fclose (png);
    return 4;
  }


#ifdef GRR_16BIT_DEBUG
  msb_max = lsb_max = 0;
  msb_min = lsb_min = 255;
  s16_max = 0;
  s16_min = 65535;
#endif

  for (pass = 0 ; pass < png_set_interlace_handling (png_ptr) ; pass++)
  {
    for (row = 0; row < rows; row++)
    {
      if (planar == 1) /* contiguous picture */
      {
        if (!tiled)
        {
	  if (TIFFReadScanline (tif, tiffline, row, 0) < 0)
	  {
            fprintf (stderr, "tiff2png error:  bad data read on line %d (%s)\n",
              row, tiffname);
            png_destroy_write_struct (&png_ptr, &info_ptr);
            TIFFClose (tif);
            free(tiffline);
            fclose (png);
	    return 1;
	  }
        }
        else /* tiled */
        {
          int col, ok=1, r;
          int tileno;
          /* FAP 20020610 - Read in one row of tiles and hand out the data one
                            scanline at a time so the code below doesn't need
                            to change */
          /* Is it time for a new strip? */
          if ((row % tile_height) == 0)
          {
            for (col = 0; ok && col < num_tilesX; col += 1 )
            {
              tileno = col+(row/tile_height)*num_tilesX;
/*            fprintf(stderr,"\nRow:%d Col:%d Tile No: %d ",row,col,tileno); */
              /* read the tile into an RGB array */
              if (!TIFFReadEncodedTile(tif, tileno, tifftile, tilesz))
              {
                ok = 0;
                break;
              }

              /* copy this tile into the row buffer */
              for (r = 0; r < (int) tile_height; r++)
              {
                void* dest;
                void* src;

                dest = tiffstrip + (r * tile_width * num_tilesX * spp)
                                 + (col * tile_width * spp);
                src  = tifftile + (r * tile_width * spp);
/*              if (row == 0) fprintf(stderr, "\ndest: %p src:%p", dest,src); */
                memcpy(dest, src, (tile_width * spp));
              }
            }
            tiffline = tiffstrip; /* set tileline to top of strip */
/*          fprintf(stderr, "\ntiffline:%p", tiffline); */
          }
          else
          {
            tiffline = tiffstrip +
              ((row % tile_height) * ((tile_width * num_tilesX) * spp));
/*          fprintf(stderr,"\ntiffline:%p %d",tiffline,(tiffline-tiffstrip)); */
          }
        } /* end if (tiled) */
      }
      else /* separated planes, then combine more strips into one line */
      {
        ush s;

        /* XXX:  this assumes strips; are separated-plane tiles possible? */

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
            fprintf (stderr, "tiff2png error:  bad data read on line %d (%s)\n",
              row, tiffname);
            png_destroy_write_struct (&png_ptr, &info_ptr);
            TIFFClose (tif);
            free(tiffline);
            free(tiffstrip);
            fclose (png);
	    return 1;
	  }

	  p_strip = (uch *)tiffstrip;
	  sample = '\0';
          for (i = 0 ; i < s ; i++)
            PUT_LINE_SAMPLE
	  for (n = 0; n < cols; n++)
	  {
	    GET_STRIP_SAMPLE
	    PUT_LINE_SAMPLE
	    sample = '\0';
	    for (i = 0 ; i < (spp-1) ; i++)
	      PUT_LINE_SAMPLE
	  }
	} /* end for-loop (s) */
      } /* end if (planar/contiguous) */

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
#ifdef INVERT_MINISWHITE
		if (photometric == PHOTOMETRIC_MINISWHITE)
		{
                  if (bigendian)		/* same as PNG order */
                  {
		    GET_LINE_SAMPLE
		    sample16 = sample;
		    sample16 <<= 8;
		    GET_LINE_SAMPLE
		    sample16 |= sample;
                  }
                  else				/* reverse of PNG */
                  {
		    GET_LINE_SAMPLE
		    sample16 = sample;
#ifdef GRR_16BIT_DEBUG
                    if (msb_max < sample)
                      msb_max = sample;
                    if (msb_min > sample)
                      msb_min = sample;
#endif
		    GET_LINE_SAMPLE
		    sample16 |= (((int)sample) << 8);
#ifdef GRR_16BIT_DEBUG
                    if (lsb_max < sample)
                      lsb_max = sample;
                    if (lsb_min > sample)
                      lsb_min = sample;
#endif
                  }
		  sample16 = maxval - sample16;
#ifdef GRR_16BIT_DEBUG
                  if (s16_max < sample16)
                    s16_max = sample16;
                  if (s16_min > sample16)
                    s16_min = sample16;
#endif
		  *p_png++ = (uch)((sample16 >> 8) & 0xff);
	  	  *p_png++ = (uch)(sample16 & 0xff);
		}
		else /* not PHOTOMETRIC_MINISWHITE */
#endif /* INVERT_MINISWHITE */
		{
                  if (bigendian)
                  {
		    GET_LINE_SAMPLE
		    *p_png++ = sample;
		    GET_LINE_SAMPLE
		    *p_png++ = sample;
                  }
                  else
                  {
                    GET_LINE_SAMPLE
                    p_png[1] = sample;
                    GET_LINE_SAMPLE
                    *p_png = sample;
                    p_png += 2;
                  }
                } /* ? PHOTOMETRIC_MINISWHITE */
		break;

              case 8:
              case 4:
              case 2:
              case 1:
		GET_LINE_SAMPLE
#ifdef INVERT_MINISWHITE
		if (photometric == PHOTOMETRIC_MINISWHITE)
		  sample = maxval - sample;
#endif
	        *p_png++ = sample;
		break;

            } /* end switch (bps) */
	  }
          /* note that this actually converts 1-bit grayscale to 2-bit indexed
           * data, where 0 = black, 1 = half-gray (127), and 2 = white */
          if (faxpect)
          {
	    png_byte *p_png2;

	    p_png = pngline;
	    p_png2 = pngline;
	    for (col = halfcols; col > 0; --col)
            {
	      *p_png++ = p_png2[0] + p_png2[1];
	      p_png2 += 2;
            }
          }
	  break;

/*
        XXX BUG:  this doesn't check for associated vs. unassociated alpha

	GRR PSEUDO-FIX 20001109:  from tiff2ps.c:
	     TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES,
	       &extrasamples, &sampleinfo);
	     if (extrasamples > 1) {
	       warn&die:  unknown extra-sample type
	     } else if (sampleinfo[0] == EXTRASAMPLE_ASSOCALPHA) {
	       warn&die or warn & do (lossy) conversion of gray/RGB samples
	     } else if (sampleinfo[0] == EXTRASAMPLE_UNSPECIFIED) {
	       warn but continue (assume unassociated alpha)
	     } else if (sampleinfo[0] == EXTRASAMPLE_UNASSALPHA) {
	       much happiness
	     } else {
	       warn&die:  unknown extra-sample type
	     }
 */
        case PNG_COLOR_TYPE_GRAY_ALPHA:
	  for (col = 0; col < cols; col++)
	  {
            for (i = 0 ; i < spp ; i++)
            {
              switch (bps)
	      {
                case 16:
#ifdef INVERT_MINISWHITE	/* GRR 20000122:  XXX 16-bit case not tested */
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		  {
                    if (bigendian)
                    {
		      GET_LINE_SAMPLE
		      sample16 = (sample << 8);
		      GET_LINE_SAMPLE
		      sample16 |= sample;
                    }
                    else
                    {
		      GET_LINE_SAMPLE
		      sample16 = sample;
		      GET_LINE_SAMPLE
		      sample16 |= (((int)sample) << 8);
                    }
		    sample16 = maxval - sample16;
		    *p_png++ = (uch)((sample16 >> 8) & 0xff);
	  	    *p_png++ = (uch)(sample16 & 0xff);
		  }
		  else
#endif
		  {
                    if (bigendian)
                    {
		      GET_LINE_SAMPLE
		      *p_png++ = sample;
		      GET_LINE_SAMPLE
		      *p_png++ = sample;
                    }
                    else
                    {
                      GET_LINE_SAMPLE
                      p_png[1] = sample;
                      GET_LINE_SAMPLE
                      *p_png = sample;
                      p_png += 2;
                    }
		  }
		  break;

                case 8:
		  GET_LINE_SAMPLE
#ifdef INVERT_MINISWHITE
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample;
		  break;

                case 4:
		  GET_LINE_SAMPLE
#ifdef INVERT_MINISWHITE
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample * 17;	/* was 16 */
		  break;

                case 2:
		  GET_LINE_SAMPLE
#ifdef INVERT_MINISWHITE
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample * 85;	/* was 64 */
		  break;

                case 1:
		  GET_LINE_SAMPLE
#ifdef INVERT_MINISWHITE
		  if (photometric == PHOTOMETRIC_MINISWHITE && i == 0)
		    sample = maxval - sample;
#endif
	          *p_png++ = sample * 255;	/* was 128...oops */
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
                  /* XXX:  do we need INVERT_MINISWHITE support here, too, or
		   *       is that only for grayscale? */
                  if (bigendian)
                  {
		    GET_LINE_SAMPLE
		    *p_png++ = sample;
		    GET_LINE_SAMPLE
		    *p_png++ = sample;
		  }
		  else
		  {
		    GET_LINE_SAMPLE
		    p_png[1] = sample;
		    GET_LINE_SAMPLE
		    *p_png = sample;
		    p_png += 2;
		  }
		  break;

                case 8:
		  GET_LINE_SAMPLE
	          *p_png++ = sample;
		  break;

                /* XXX:  how common are these three cases? */

                case 4:
		  GET_LINE_SAMPLE
	          *p_png++ = sample * 17;	/* was 16 */
		  break;

                case 2:
		  GET_LINE_SAMPLE
	          *p_png++ = sample * 85;	/* was 64 */
		  break;

                case 1:
		  GET_LINE_SAMPLE
	          *p_png++ = sample * 255;	/* was 128 */
		  break;

              } /* end switch */
		
            }
	  }
	  break;
  
        case PNG_COLOR_TYPE_PALETTE:
	  for (col = 0; col < cols; col++)
	  {
	    GET_LINE_SAMPLE
            *p_png++ = sample;
	  }
	  break;
  
	default:
	{
          fprintf (stderr, "tiff2png error:  unknown photometric (%d) (%s)\n",
            photometric, tiffname);
          png_destroy_write_struct (&png_ptr, &info_ptr);
          TIFFClose (tif);
          free (tiffline);
          if (tiled && planar == 1)
            free(tifftile);
          else if (planar != 1)
            free(tiffstrip);
          fclose (png);
	  return 1;
	}

      } /* end switch (tiff_color_type) */

#ifdef GRR_16BIT_DEBUG
      if (verbose && bps == 16 && row == 0)
      {
        fprintf (stderr, "DEBUG:  hex contents of first row sent to libpng:\n");
        p_png = pngline;
        for (col = cols; col > 0; --col, p_png += 2)
          fprintf (stderr, "   %02x %02x", p_png[0], p_png[1]);
        fprintf (stderr, "\n");
        fprintf (stderr, "DEBUG:  end of first row sent to libpng\n");
        fflush (stderr);
      }
#endif

      png_write_row (png_ptr, pngline);

    } /* end for-loop (row) */
  } /* end for-loop (pass) */

  TIFFClose(tif);

  png_write_end (png_ptr, info_ptr);
  fclose (png);

  png_destroy_write_struct (&png_ptr, &info_ptr);

  free(tiffline);
  if (tiled && planar == 1)
    free(tifftile);
  else if (planar != 1)
    free(tiffstrip);

#ifdef GRR_16BIT_DEBUG
  if (verbose && bps == 16)
  {
    fprintf (stderr, "tiff2png:  range of most significant bytes  = %u-%u\n",
      msb_min, msb_max);
    fprintf (stderr, "tiff2png:  range of least significant bytes = %u-%u\n",
      lsb_min, lsb_max);
    fprintf (stderr, "tiff2png:  range of 16-bit integer values   = %u-%u\n",
      s16_min, s16_max);
  }
#endif

  if (verbose)
    fprintf (stderr, "\n");

  return 0;
}

/*----------------------------------------------------------------------------*/

int
main (argc, argv)
  int argc;
  char *argv[];
{
  char *tiffname;
  char *pngname;
  char *basename = NULL;
  char *destdir = NULL;
  int destlen = 0;
  int len;
  int argn = 1;
  int compression_level = -1;
  int interlace_type = PNG_INTERLACE_NONE;
  int verbose = FALSE;
  int force = FALSE;
  int invert = FALSE;
  int faxpect = FALSE;
  double gamma = -1.0;


#ifdef __EMX__
  _wildcard(&argc, &argv);   /* Unix-like globbing for OS/2 and DOS */
#endif

  /* debug */

  if (verbose)
  {
    fprintf (stderr, "tiff2png:  new libtiff (like v3.4) is used\n");
  }

  /* get command-line arguments */

  if (argn == argc)
    usage (0);

  while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0')
  {
    if (strncmp (argv[argn], "-help", 2) == 0)
      usage (0);
    else if (strncmp (argv[argn], "-verbose", 2) == 0)
      verbose = TRUE;
    else if (strncmp (argv[argn], "-compression", 2) == 0)
    {
      if (++argn < argc)
	sscanf (argv[argn], "%d", &compression_level);
      else
	usage (1);
      if (compression_level < 0 || compression_level > 9)
      {
        fprintf (stderr,
          "tiff2png error:  compression level must be between 0 and 9\n");
	usage (1);
      }
    }
    else if (strncmp (argv[argn], "-destdir", 2) == 0)
    {
      if (++argn < argc)
	destdir = argv[argn];
      else
	usage (1);
    }
    else if (strncmp (argv[argn], "-force", 3) == 0)
      force = TRUE;
    else if (strncmp (argv[argn], "-gamma", 2) == 0)
    {
      if (++argn < argc)
	sscanf (argv[argn], "%lf", &gamma);
      else
	usage (1);
      if (gamma <= 0.0)
      {
        fprintf (stderr,
          "tiff2png error:  gamma value must be greater than zero\n");
	usage (1);
      }
    }
    else if (strncmp (argv[argn], "-interlace", 4) == 0)
      interlace_type = PNG_INTERLACE_ADAM7;
    else if (strncmp (argv[argn], "-invert", 4) == 0)
      invert = TRUE;
    else if (strncmp (argv[argn], "-faxpect", 3) == 0)
      faxpect = TRUE;
    else
      usage (1);
    argn++;
  }


#ifdef DEFAULT_DESTDIR_IS_CURDIR
  /* SJT: I like always writing to the current directory. */
  if (destdir == NULL)
    destdir = ".";
#endif

  /* If destdir exists, then it is essential that the concatenated output
   * name be of the form "destdir DIR_SEP basename".  You cannot depend on
   * the user supplying a DIR_SEP.  We remove a trailing one (if any) from
   * destdir and remove any leading one from basename.  Then we add it back.
   */
  if (destdir)
  {
    destlen = strlen(destdir);
    if (destdir[destlen - 1] == DIR_SEP)
	destlen--;
  }

  while (argn < argc)
  {
    tiffname = argv[argn];
    if (destdir)
    {
      basename = strrchr(argv[argn], DIR_SEP);
      if (!basename)
        basename = tiffname;
      else
        basename++;				/* skip the DIR_SEP */

      len = destlen + strlen(basename) + 1;	/* DIR_SEP better be one char */
    }
    else
      len = strlen(tiffname);

    pngname = (char *)malloc(len+5);	/* room for appended ".png\0" */
    if (pngname == NULL)
    {
      fprintf (stderr,
        "tiff2png error:  can't allocate memory for pngname buffer\n");
      return 4;
    }

    if (destdir)
    {
      strcpy(pngname, destdir);
      pngname[destlen] = DIR_SEP;
      strcpy(pngname+destlen+1, basename);	/* 1 for DIR_SEP */
    }
    else
      strcpy(pngname, tiffname);

    if (strcasecmp(pngname+len-5, ".tiff") == 0)
      strcpy(pngname+len-5, ".png");
    else if (strcasecmp(pngname+len-4, ".tif") == 0)
      strcpy(pngname+len-4, ".png");
    else
      strcpy(pngname+len, ".png");

    tiff2png(tiffname, pngname, verbose, force, interlace_type,
      compression_level, invert, faxpect, gamma);

    free(pngname);
    argn++;
  }

  return 0;
}
