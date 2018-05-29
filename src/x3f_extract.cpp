/* X3F_EXTRACT.C
 *
 * Extracting images from X3F files.
 * Also converting to histogram and color image.
 *
 * Copyright 2015 - Roland and Erik Karlsson
 * BSD-style - see doc/copyright.txt
 *
 */
extern "C" {
#include "x3f_version.h"
#include "x3f_io.h"
#include "x3f_process.h"
#include "x3f_output_dng.h"
#include "x3f_output_tiff.h"
#include "x3f_output_ppm.h"
#include "x3f_histogram.h"
#include "x3f_print_meta.h"
#include "x3f_dump.h"
#include "x3f_denoise.h"
#include "x3f_printf.h"
}

#include <string>
#include <iostream>
#include <boost/program_options.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
  { META      = 0,
    JPEG      = 1,
    RAW       = 2,
    TIFF      = 3,
    DNG       = 4,
    PPMP3     = 5,
    PPMP6     = 6,
    HISTOGRAM = 7}
  output_file_type_t;

static const char *extension[] =
  { ".meta",
    ".jpg",
    ".raw",
    ".tif",
    ".dng",
    ".ppm",
    ".ppm",
    ".csv" };

static const char Desc[] = {
    "usage: x3f_extract <SWITCHES> <file1> ...\n"
};

namespace {

  void add_options( boost::program_options::options_description& desc ) {
    desc.add_options()
      ("help,h",   "produce help message")
      ("output,o", boost::program_options::value<std::string>(), "Output directory")
      ("debug,d", "Verbose output for debugging")
      ("quiet,q", "Suppress all messages except errors")
      ("format,f", boost::program_options::value<std::string>(), "Extract file format...\n"
	      "meta      : Dump metadata\n"
        "jpg       : Dump embedded JPEG\n"
        "raw       : Dump RAW area undecoded\n"
        "tiff      : Dump RAW/color as 3x16 bit TIFF\n"
        "dng       : Dump RAW as DNG LinearRaw (default)\n"
        "ppm-ascii : Dump RAW/color as 3x16 bit PPM/P3 (ascii)\n"
        "             16 bit PPM/P3 is not generally supported\n"
        "ppm       : Dump RAW/color as 3x16 bit PPM/P6 (binary)\n"
        "histogram : Dump histogram as csv file\n"
        "loghist   : Dump histogram as csv file,\n"
        "              with log exposure\n"
      )
      ("color,c", boost::program_options::value<std::string>(), "Convert to RGB color space...\n"
        "none        : means neither scaling, applying gamma\n"
        "sRGB        : sRGB color space\n"
        "AdobeRGB    : AdobeRGB color space\n"
        "ProPhotoRGB : ProPhotoRGB color space\n"
        "unprocessed : Dump RAW without any preprocessing\n"
        "qtop        : Dump Quattro top layer without preprocessing\n"
        " * This switch does not affect DNG output\n"
      )
      ("noCrop,r",    "Do not crop to active area")
      ("noDenoise",   "Do not denoise RAW data")
      ("noSGain",     "Do not apply spatial gain (color compensation)")
      ("sgain",       "Apply spatial gain (color compensation)"
                      "  (Apply except for Quattro)"
      )
      ("noFixBad,b",    "Do not fix bad pixels")
      ("whiteBalance,w", boost::program_options::value<std::string>(), "Select white balance preset\n"
        "Auto:         Auto WB\n"
        "Sunlight:     Sun light WB\n"
        "Shadow:       Shadow WB\n"
        "Overcast:     Overcast WB\n"
        "Incandescent: Incandescent WB\n"
        "Florescent:   Florescent WB\n"
        "Flash:        For Flash WB\n"
        "Custom:       Custom WB\n"
        "ColorTemp:    Color Temperture WB\n"
        "AutoLSP:      Auto LSP WB\n"
      )
      ("compress,z",     "Enable ZIP compression for DNG and TIFF output")
      ("ocl",            "Use OpenCL")
      ("offset",         boost::program_options::value<int>(), "Offset for SD14 and older\n"
                         " NOTE: If not given, then offset is automatic\n"
      )
      ("matrixmax",      boost::program_options::value<int>(), "Max num matrix elements in metadata (def=100)")
      ("input",      boost::program_options::value<std::string>()) //, "input x3f files" )
    ;	  
  }
  
}

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static int check_dir(const char *Path)
{
  struct stat filestat;
  int ret;

  if ((ret = stat(Path, &filestat)) != 0)
    return ret;

  if (!S_ISDIR(filestat.st_mode)) {
    errno = ENOTDIR;
    return -1;
  }

  return 0;
}

#define MAXPATH 1000
#define EXTMAX 10
#define MAXOUTPATH (MAXPATH+EXTMAX)
#define MAXTMPPATH (MAXOUTPATH+EXTMAX)

static int safecpy(char *dst, const char *src, int dst_size)
{
  if (strnlen(src, dst_size+1) > dst_size) {
    x3f_printf(DEBUG, "safecpy: String too large\n");
    return 1;
  } else {
    strcpy(dst, src);
    return 0;
  }
}

static int safecat(char *dst, const char *src, int dst_size)
{
  if (strnlen(dst, dst_size+1) + strnlen(src, dst_size+1) > dst_size) {
    x3f_printf(DEBUG, "safecat: String too large\n");
    return 1;
  } else {
    strcat(dst, src);
    return 0;
  }
}

#if defined(_WIN32) || defined(_WIN64)
#define PATHSEP "\\"
static const char pathseps[] = PATHSEP "/:";
#else
#define PATHSEP "/"
static const char pathseps[] = PATHSEP;
#endif

static int make_paths(const char *inpath, const char *outdir,
		      const char *ext,
		      char *tmppath, char *outpath)
{
  int err = 0;

  if (outdir && *outdir) {
    const char *ptr = inpath, *sep, *p;

    for (sep=pathseps; *sep; sep++)
      if ((p = strrchr(inpath, *sep)) && p+1 > ptr) ptr = p+1;

    err += safecpy(outpath, outdir, MAXOUTPATH);
    if (!strchr(pathseps, outdir[strlen(outdir)-1]))
      err += safecat(outpath, PATHSEP, MAXOUTPATH);
    err += safecat(outpath, ptr, MAXOUTPATH);
  }
  else err += safecpy(outpath, inpath, MAXOUTPATH);

  err += safecat(outpath, ext, MAXOUTPATH);
  err += safecpy(tmppath, outpath, MAXTMPPATH);
  err += safecat(tmppath, ".tmp", MAXTMPPATH);

  return err;
}

#define Z extract_jpg=0,extract_raw=0,extract_unconverted_raw=0

int main(int argc, char *argv[])
{
  int extract_jpg = 0;
  int extract_meta; /* Always computed */
  int extract_raw = 1;
  int extract_unconverted_raw = 0;
  int crop = 1;
  int fix_bad = 1;
  int denoise = 1;
  int apply_sgain = -1;
  output_file_type_t file_type = DNG;
  x3f_color_encoding_t color_encoding = SRGB;
  int files = 0;
  int errors = 0;
  int log_hist = 0;
  std::string whiteBalance;
  char *wb = NULL;
  int compress = 0;
  int use_opencl = 0;
  std::string soutdir;
  char *outdir = NULL;
  x3f_return_t ret;

  x3f_printf(INFO, "X3F TOOLS VERSION = %s\n\n", version);

  /* Set stdout and stderr to line buffered mode to avoid scrambling */
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IOLBF, 0);

  boost::program_options::options_description desc( Desc );
  boost::program_options::positional_options_description pos;
  pos.add("input", -1);
  add_options( desc );

	boost::program_options::variables_map	vm;
	boost::program_options::store( 
		boost::program_options::command_line_parser( argc, argv ).options( desc ).positional(pos).run(),
		vm
	);
	boost::program_options::notify( vm );

  if( vm.count("help") ) std::cerr << desc;

  if( vm.count("format") ) {
	  std::string fmt = vm["format"].as<std::string>();
	  Z;
    if( fmt == "jpg" )             extract_jpg = 1,              file_type = JPEG;
    else if( fmt == "meta" )                                     file_type = META;
    else if( fmt == "raw" )        extract_unconverted_raw = 1,  file_type = RAW;
    else if( fmt == "tiff" )       extract_raw = 1,              file_type = TIFF;
    else if( fmt == "dng" )        extract_raw = 1,              file_type = DNG;
    else if( fmt == "ppm-ascii" )  extract_raw = 1,              file_type = PPMP3;
    else if( fmt == "ppm" )        extract_raw = 1,              file_type = PPMP6;
    else if( fmt == "histogram" )  extract_raw = 1,              file_type = HISTOGRAM;
    else if( fmt == "loghist" )    extract_raw = 1,              file_type = HISTOGRAM, log_hist = 1;
    else {
	    std::cerr << desc << std::endl;
	    std::cerr << "Unknown format : " << fmt << std::endl;
	    return 1;
    }
  }
  if( vm.count("color") ) {
	  std::string spc = vm["color"].as<std::string>();
	  if( spc == "none" )             color_encoding = NONE;
	  else if( spc == "sRGB" )        color_encoding = SRGB;
    else if( spc == "AdobeRGB" )	  color_encoding = ARGB;
    else if( spc == "ProPhotoRGB" ) color_encoding = PPRGB;
    else if( spc == "unprocessed" ) color_encoding = UNPROCESSED;
    else if( spc == "qtop" )        color_encoding = QTOP;
    else {
	    std::cerr << desc << std::endl;
	    std::cerr << "Unknown color encoding: " << spc << std::endl;
	    return 1;
    }
  }
  if( vm.count("debug") )     x3f_printf_level = DEBUG;
  if( vm.count("quiet") )     x3f_printf_level = ERR;
  if( vm.count("noCrop") )    crop = 0;
  if( vm.count("noFixBad") )  fix_bad = 0;
  if( vm.count("noDenoise") ) denoise = 0;
  if( vm.count("noSGain") )   apply_sgain = 0;
  if( vm.count("sgain") )     apply_sgain = 1;
  if( vm.count("whiteBalance") ) {
	  whiteBalance = vm["whiteBalance"].as<std::string>();
	  wb = const_cast<char*>(whiteBalance.c_str());
  }
  if( vm.count("compress") )  compress = 1;
  if( vm.count("ocl") )       use_opencl = 1;
  if( vm.count("offset") ) {
	  auto_legacy_offset = 0;
	  legacy_offset = vm["offset"].as<int>();
  }
  if( vm.count("matrixmax") ) {
	  max_printed_matrix_elements = vm["matrixmax"].as<int>();
  }
  if( vm.count("output") ) {
	  soutdir = vm["output"].as<std::string>();
	  outdir = const_cast<char*>(soutdir.c_str());
	  if( check_dir(outdir) != 0 ) {
		  std::cerr << desc << std::endl;
	  }
  }
  if( !vm.count("input") ) {
	  std::cerr << desc << std::endl;
    x3f_printf(ERR, "Could not find outdir %s\n", outdir);
    return 1;
  }

  //int i;

  //for (i=1; i<argc; i++)

  x3f_set_use_opencl(use_opencl);

  extract_meta =
    file_type == META ||
    file_type == DNG ||
    (extract_raw &&
     (crop || (color_encoding != UNPROCESSED && color_encoding != QTOP)));

  //for (; i<argc; i++) {
    std::string ifile = vm["input"].as<std::string>();
    char *infile = const_cast<char*>(ifile.c_str());
    //argv[i];
    FILE *f_in = fopen(infile, "rb");
    x3f_t *x3f = NULL;

    char tmpfile[MAXTMPPATH+1];
    char outfile[MAXOUTPATH+1];
    x3f_return_t ret_dump;
    int sgain;

    files++;

    if (f_in == NULL) {
      x3f_printf(ERR, "Could not open infile %s\n", infile);
      goto found_error;
    }

    x3f_printf(INFO, "READ THE X3F FILE %s\n", infile);
    x3f = x3f_new_from_file(f_in);

    if (x3f == NULL) {
      x3f_printf(ERR, "Could not read infile %s\n", infile);
      goto found_error;
    }

    if (extract_jpg) {
      if (X3F_OK != (ret = x3f_load_data(x3f, x3f_get_thumb_jpeg(x3f)))) {
	x3f_printf(ERR, "Could not load JPEG thumbnail from %s (%s)\n",
		   infile, x3f_err(ret));
	goto found_error;
      }
    }

    if (extract_meta) {
      x3f_directory_entry_t *DE = x3f_get_prop(x3f);

      if (X3F_OK != (ret = x3f_load_data(x3f, x3f_get_camf(x3f)))) {
	x3f_printf(ERR, "Could not load CAMF from %s (%s)\n",
		   infile, x3f_err(ret));
	goto found_error;
      }
      if (DE != NULL)
	/* Not for Quattro */
	if (X3F_OK != (ret = x3f_load_data(x3f, DE))) {
	  x3f_printf(ERR, "Could not load PROP from %s (%s)\n",
		     infile, x3f_err(ret));
	  goto found_error;
	}
      /* We do not load any JPEG meta data */
    }

    if (extract_raw) {
      x3f_directory_entry_t *DE;

      if (NULL == (DE = x3f_get_raw(x3f))) {
	x3f_printf(ERR, "Could not find any matching RAW format\n");
	goto found_error;
      }

      if (X3F_OK != (ret = x3f_load_data(x3f, DE))) {
	x3f_printf(ERR, "Could not load RAW from %s (%s)\n",
		   infile, x3f_err(ret));
	goto found_error;
      }
    }

    if (extract_unconverted_raw) {
      x3f_directory_entry_t *DE;

      if (NULL == (DE = x3f_get_raw(x3f))) {
	x3f_printf(ERR, "Could not find any matching RAW format\n");
	goto found_error;
      }

      if (X3F_OK != (ret = x3f_load_image_block(x3f, DE))) {
	x3f_printf(ERR, "Could not load unconverted RAW from %s (%s)\n",
		   infile, x3f_err(ret));
	goto found_error;
      }
    }

    if (make_paths(infile, outdir, extension[file_type], tmpfile, outfile)) {
      x3f_printf(ERR, "Too large outfile path for infile %s and outdir %s\n",
		 infile, outdir);
      goto found_error;
    }

    unlink(tmpfile);

    /* TODO: Quattro files seem to be already corrected for spatial
       gain. Is that assumption correct? Applying it only worsens the
       result anyhow, so it is disabled by default. */
    sgain =
      apply_sgain == -1 ? x3f->header.version < X3F_VERSION_4_0 : apply_sgain;

    switch (file_type) {
    case META:
      x3f_printf(INFO, "Dump META DATA to %s\n", outfile);
      ret_dump = x3f_dump_meta_data(x3f, tmpfile);
      break;
    case JPEG:
      x3f_printf(INFO, "Dump JPEG to %s\n", outfile);
      ret_dump = x3f_dump_jpeg(x3f, tmpfile);
      break;
    case RAW:
      x3f_printf(INFO, "Dump RAW block to %s\n", outfile);
      ret_dump = x3f_dump_raw_data(x3f, tmpfile);
      break;
    case TIFF:
      x3f_printf(INFO, "Dump RAW as TIFF to %s\n", outfile);
      ret_dump = x3f_dump_raw_data_as_tiff(x3f, tmpfile,
					   color_encoding,
					   crop, fix_bad, denoise, sgain, wb,
					   compress);
      break;
    case DNG:
      x3f_printf(INFO, "Dump RAW as DNG to %s\n", outfile);
      ret_dump = x3f_dump_raw_data_as_dng(x3f, tmpfile,
					  fix_bad, denoise, sgain, wb,
					  compress);
      break;
    case PPMP3:
    case PPMP6:
      x3f_printf(INFO, "Dump RAW as PPM to %s\n", outfile);
      ret_dump = x3f_dump_raw_data_as_ppm(x3f, tmpfile,
					  color_encoding,
					  crop, fix_bad, denoise, sgain, wb,
					  file_type == PPMP6);
      break;
    case HISTOGRAM:
      x3f_printf(INFO, "Dump RAW as CSV histogram to %s\n", outfile);
      ret_dump = x3f_dump_raw_data_as_histogram(x3f, tmpfile,
						color_encoding,
						crop, fix_bad, denoise, sgain, wb,
						log_hist);
      break;
    }

    if (X3F_OK != ret_dump) {
      x3f_printf(ERR, "Could not dump to %s: %s\n", tmpfile, x3f_err(ret_dump));
      errors++;
    } else {
      if (rename(tmpfile, outfile) != 0) {
	x3f_printf(ERR, "Could not rename %s to %s\n", tmpfile, outfile);
	errors++;
      }
    }

    goto clean_up;

  found_error:

    errors++;

  clean_up:

    x3f_delete(x3f);

    if (f_in != NULL)
      fclose(f_in);
  //}

  if (files == 0) {
    x3f_printf(ERR, "No files given\n");
      fprintf(stderr,Desc);
  }

  x3f_printf(INFO, "Files processed: %d\terrors: %d\n", files, errors);

  return errors > 0;
}
