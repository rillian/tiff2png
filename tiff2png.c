/*
** tiff2png.c - converts a Tagged Image File to a Portable Network Graphics file
**
** Copyright (C) 1996 by Willem van Schaik, Singapore
**                       <gwillem@ntuvax.ntu.ac.sg>
**
** version 0.6 - May 1996
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

#include "tiff.h"
#include "tiffio.h"
#include "tiffcomp.h"
#include "png.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NONE
#define NONE 0
#endif
#define MAXCOLORS 256
#ifndef PHOTOMETRIC_DEPTH
#define PHOTOMETRIC_DEPTH 32768
#endif

/*
typedef	unsigned char u_char;
typedef	unsigned short u_short;
typedef	unsigned int u_int;
typedef	unsigned long u_long;
*/

static int verbose = FALSE;
static int interlace = FALSE;
static float gamma = -1.0;

/* macro's to get and put bit's out of the bytes */

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

int
main (argc, argv)
  int argc;
  char* argv[];

{
  int argn;
  char* usage = "tiff2png [-verbose] [-gamma] [-interlace] <tiff-file> <png-file>";

  register TIFF* tif;
  char name [256];
  u_short bps, spp, planar;
  u_short photometric;
  int maxval;
  int colors;
  int cols, rows;
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

  unsigned short* redcolormap;
  unsigned short* greencolormap;
  unsigned short* bluecolormap;

  FILE *png;
  FILE *fopen();
  png_struct *png_ptr;
  png_info *info_ptr;
  png_byte *pngline;
  png_byte *p_png;
  png_color palette[MAXCOLORS];
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
    {
      fprintf (stderr, "Usage: %s\n", usage);
      exit (0);
    }
    else if (strncmp (argv[argn], "-verbose", 2) == 0)
      verbose = TRUE;
    else if (strncmp (argv[argn], "-gamma", 2) == 0)
    {
      if (++argn < argc)
	sscanf (argv[argn], "%f", &gamma);
      else
      {
	fprintf (stderr, "Usage: %s\n", usage);
	exit (1);
      }
    }
    else if (strncmp (argv[argn], "-interlace", 2) == 0)
      interlace = TRUE;
    else
    {
      fprintf (stderr, "Usage: %s\n", usage);
      exit (1);
    }
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
  {
    fprintf (stderr,  "Usage: %s\n", usage);
    exit (1);
  }

  if (argn != argc)
  {
    png = fopen (argv[argn], "wb");
    if (png == NULL)
    {
      fprintf (stderr, "Error: PNG file %s can not be created\n", argv[argn]);
      exit (1);
    }
    argn++;
  }
  else
  {
    fprintf (stderr,  "Usage: %s\n", usage);
    exit (1);
  }

  if (argn != argc)
  {
    fprintf (stderr,  "Usage: %s\n", usage);
    exit (1);
  }

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

  if (verbose)
  {
    fprintf (stderr, "Tiff2png: %dx%dx%d image\n", cols, rows, bps * spp);
    fprintf (stderr, "Tiff2png: %d bits/sample, %d samples/pixel\n", bps, spp);
  }

  /* start PNG preparation */

  png_ptr = (png_struct *)malloc (sizeof (png_struct));
  info_ptr = (png_info *)malloc (sizeof (png_info));

  if (png_ptr == NULL || info_ptr == NULL)
    fprintf (stderr, "Error: cannot allocate PNGLIB structures\n");

  if (setjmp (png_ptr->jmpbuf))
  {
    png_write_destroy (png_ptr);
    free (png_ptr);
    free (info_ptr);
    fprintf (stderr, "Error: setjmp returns error condition\n");
    exit (1);
  }

  png_write_init (png_ptr);
  png_info_init (info_ptr);
  png_init_io (png_ptr, png);
  info_ptr->width = cols;
  info_ptr->height = rows;

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
	info_ptr->color_type = PNG_COLOR_TYPE_GRAY;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = grayscale\n");
	info_ptr->bit_depth = bps;
      }
      else /* must be alpha */
      {
	info_ptr->color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = grayscale + alpha\n");
	if (bps <= 8)
	  info_ptr->bit_depth = 8;
	else
	  info_ptr->bit_depth = bps;
      }
      break;

    case PHOTOMETRIC_PALETTE:
    {
      info_ptr->color_type = PNG_COLOR_TYPE_PALETTE;
      if (verbose)
	fprintf (stderr, "Tiff2png: color-type = paletted\n");

      if (! TIFFGetField (tif, TIFFTAG_COLORMAP, &redcolormap, &greencolormap, &bluecolormap))
      {
	fprintf (stderr, "Error: can not retrieve colormaps\n");
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
	info_ptr->bit_depth = 8;
      else
	info_ptr->bit_depth = bps;

      /* PLTE chunk */
      /* tiff-palettes contain 16-bit shorts, while png-palettes are 8-bit) */
      for (i = 0 ; i < colors ; i++)
      {
	palette[i].red   = (png_byte) (redcolormap[i] >> 8);
	palette[i].green = (png_byte) (greencolormap[i] >> 8);
	palette[i].blue  = (png_byte) (bluecolormap[i] >> 8);
      }
      info_ptr->valid |= PNG_INFO_PLTE;
      info_ptr->palette = palette;
      info_ptr->num_palette = colors;
      break;
    }

    case PHOTOMETRIC_RGB:
      if (spp == 3)
      {
	info_ptr->color_type = PNG_COLOR_TYPE_RGB;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = truecolor\n");
      }
      else
      {
	info_ptr->color_type = PNG_COLOR_TYPE_RGB_ALPHA;
	if (verbose)
	  fprintf (stderr, "Tiff2png: color-type = truecolor + alpha\n");
      }
      if (bps <= 8)
	info_ptr->bit_depth = 8;
      else
	info_ptr->bit_depth = bps;
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

  if (verbose)
    fprintf (stderr, "Tiff2png: bit-depth = %d\n", info_ptr->bit_depth);

  /* put parameter info in png-chunks */

  info_ptr->interlace_type = interlace;

  /* gAMA chunk */
  if (gamma != -1.0)
  {
    info_ptr->valid |= PNG_INFO_gAMA;
    info_ptr->gamma = gamma;
    if (verbose)
      fprintf (stderr, "Tiff2png: gamma=%f\n", gamma);
  }

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

      switch (info_ptr->color_type)
      {
        case PNG_COLOR_TYPE_GRAY:
	  for (col = 0; col < cols; col++)
	  {
            for (i = 0 ; i < spp ; i++)
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
	  }
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
  png_write_destroy (png_ptr);

  free (png_ptr);
  free (info_ptr);

  close (png);
  exit (0);
}

