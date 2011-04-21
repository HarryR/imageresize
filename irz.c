/*
Copyright (c) 2009 ECMP Ltd.
Copyright (c) 2010,2011 Harry Roberts.
Copyright (c) 2011 Cal Leeming.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <assert.h>
#include <errno.h>

#include "irz.h"

bool
irz_log(struct irz_config *cfg, irz_error_t level, char *fmt, ...) {
	bool did_log = level >= cfg->logger_threshold;
	if( did_log && cfg->logger != NULL ) {
		va_list ap;
		va_start(ap, fmt);
		did_log = cfg->logger(cfg, level, fmt, ap);
		va_end(ap);
	}
	return did_log;
}

irz_config_t *
irz_new(void) {
	irz_config_t *cfg;
	cfg = malloc(sizeof(irz_config_t));
	assert( cfg != NULL );		
	return cfg;
}

void
irz_free(irz_config_t *cfg) {
	free(cfg);
}

void
irz_init(irz_config_t *cfg) {
	memset(cfg, 0, sizeof(irz_config_t));
}

static void
setup_out(struct jpeg_compress_struct *out_cinfo
	, struct jpeg_error_mgr *jerr
	, FILE* out_fh, int out_width
	, int out_height
	, int out_quality)
{
	out_cinfo->err = jpeg_std_error(jerr);
	jpeg_create_compress(out_cinfo);
	jpeg_stdio_dest(out_cinfo, out_fh);
	out_cinfo->image_width = out_width;
	out_cinfo->image_height = out_height;
	out_cinfo->input_components = 3;
	out_cinfo->in_color_space = JCS_RGB;

	jpeg_set_defaults(out_cinfo);
	jpeg_set_quality(out_cinfo, out_quality, 1);
	jpeg_start_compress(out_cinfo, 1);
}

static void
setup_in(struct jpeg_decompress_struct *in_dinfo, struct jpeg_error_mgr *jerr, FILE *in_fh) {
	in_dinfo->err = jpeg_std_error( jerr );
	jpeg_create_decompress( in_dinfo );

	/*
	in_dinfo->out_color_space = JCS_RGB;
	 */
	jpeg_stdio_src( in_dinfo, in_fh );
	jpeg_read_header( in_dinfo, TRUE );
	jpeg_start_decompress(in_dinfo);
}

static void
calculate_mode_crop( int dstW, int dstH, int *srcW, int *srcH, int *srcX, int *srcY ) {
	int src_min = *srcW < *srcH ? *srcW : *srcH;
	int dst_max = dstW > dstH ? dstW : dstH;
	double ratio = src_min / dst_max;
	int width = ratio * (double)dstW;
	int height = ratio * (double)dstH;
	int offset_x = (*srcW / 2) - (width / 2);
	int offset_y = (*srcH / 2) - (height / 2);
	
	*srcW = width;
	*srcH = height;
	*srcX = offset_x;
	*srcY = offset_y;
}

#define gdAlphaMax 127
/* 2.0.10: cast instead of floor() yields 35% performance improvement. Thanks to John Buckman. */
#define floor_cast(exp) ((long) exp)

static void
do_resize( struct jpeg_compress_struct *out_cinfo
	  ,struct jpeg_decompress_struct *in_dinfo
	  ,irz_config_t* config)
{
	int srcW, srcH;
	int dstX, dstY, srcX, srcY;
	int dstW, dstH;
	JSAMPROW out_row;
	
	int x, y;
	double sy1, sy2, sx1, sx2;
  	
	dstX = dstY = srcX = srcY = 0;
	
	dstW = out_cinfo->image_width;
	dstH = out_cinfo->image_height;
	srcW = in_dinfo->image_width;
	srcH = in_dinfo->image_height;
	
	if( config->mode == MODE_CROP ) {
		calculate_mode_crop( dstW, dstH, &srcW, &srcH, &srcX, &srcY );
	}
	else if( config->mode == MODE_CROPMANUAL ) {
		srcW = config->crop_x2 - config->crop_x;
		srcH = config->crop_y2 - config->crop_y;
		srcX = config->crop_x;
		srcY = config->crop_y;
	}
	
	JSAMPARRAY buffer = malloc(sizeof(JSAMPROW*) * in_dinfo->image_height);
	memset(buffer, 0, sizeof(JSAMPROW*) * in_dinfo->image_height);
	int yload_old = 0;
	unsigned int yload = 0;
	int yload2 = 0;
	out_row = malloc( dstW * 3 );
	
	/* Pre-read scanlines from the image until we're at the point where it matters */
	if( srcY > 0 ) {
		JSAMPROW tmp = malloc(in_dinfo->image_width * in_dinfo->num_components);
		int i = 0;
		for( i = 0; i < srcY; i++ ) {
			jpeg_read_scanlines(in_dinfo, &tmp, 1);
		}
		free(tmp);
	}

	for (y = dstY; (y < dstY + dstH); y++) {
		sy1 = ((double) y - (double) dstY) * (double) srcH / (double) dstH;
		sy2 = ((double) (y + 1) - (double) dstY) * (double) srcH / (double) dstH;
					
		for( yload2 = yload_old; yload2 < (int)sy1 + srcY; yload2++ ) {
			free(buffer[yload2]);
			buffer[yload2] = NULL;
		}
						
		for( yload = (int)sy1 + srcY; yload < sy2 + srcY; yload++ ) {
			if( yload < in_dinfo->output_scanline ) continue;
			JSAMPROW yload_row = malloc(in_dinfo->image_width * in_dinfo->num_components);
			jpeg_read_scanlines(in_dinfo, &yload_row, 1);
			buffer[yload] = yload_row;
		}
		yload_old = sy1 + srcY;

            	for (x = dstX; (x < dstX + dstW); x++) {
			double sx, sy;
                    	double spixels = 0;
                    	double red = 0.0, green = 0.0, blue = 0.0, alpha = 0.0;
                    	double alpha_factor, alpha_sum = 0.0, contrib_sum = 0.0;
                    	sx1 = ((double) x - (double) dstX) * (double) srcW / dstW;
                    	sx2 = ((double) (x + 1) - (double) dstX) * (double) srcW / dstW;
                    	sy = sy1;
                    	do {
				double yportion;
				if (floor_cast(sy) == floor_cast(sy1)) {
					yportion = 1.0f - (sy - floor_cast(sy));
					if (yportion > sy2 - sy1) {
						yportion = sy2 - sy1;
					}
					sy = floor_cast(sy);
                            	}
				else if (sy == floorf(sy2)) {
					yportion = sy2 - floor_cast(sy2);
                            	}
				else {
					yportion = 1.0f;
                            	}

                            	sx = sx1;
                            	do {
                                    	double xportion;
                                    	double pcontribution;
                                    	if (floorf(sx) == floor_cast(sx1)) {
                                            	xportion = 1.0f - (sx - floor_cast(sx));
                                            	if (xportion > sx2 - sx1) {
							xportion = sx2 - sx1;
						}
						sx = floor_cast(sx);
                                    	}
					else if (sx == floorf(sx2)) {
						xportion = sx2 - floor_cast(sx2);
                                    	}
					else {
                                            	xportion = 1.0f;
                                    	}
                                    	pcontribution = xportion * yportion;

					/* Write output pixel */
					JSAMPLE* p = buffer[ (int)sy + srcY ] + ((int)(sx + srcX) * in_dinfo->num_components);
					alpha_factor = gdAlphaMax * pcontribution;
				
					/* Blend surrounding pixels */
					if( in_dinfo->num_components == 1 ) {
						red += p[0] * alpha_factor;
						green += p[0] * alpha_factor;
						blue += p[0] * alpha_factor;
					}
					else {
						red += p[0] * alpha_factor;
		                                green += p[1] * alpha_factor;
		                                blue += p[2] * alpha_factor;	
					}
	                                alpha += 0xFF * pcontribution;

                                    	alpha_sum += alpha_factor;
                                    	contrib_sum += pcontribution;
                                    	spixels += xportion * yportion;
                                    	sx += 1.0f;
                            	} while (sx < sx2);

                            	sy += 1.0f;
			} while (sy < sy2);

			if (spixels != 0.0f) {
	                    red /= spixels;
	                    green /= spixels;
	                    blue /= spixels;
	                    alpha /= spixels;
	                }
	
	            	if ( alpha_sum != 0.0f) {
				if( contrib_sum != 0.0f) {
					alpha_sum /= contrib_sum;
				}
				red /= alpha_sum;
				green /= alpha_sum;
				blue /= alpha_sum;
			}
			
			/* Clamping to allow for rounding errors above */
			if (red > 255.0f) {
				red = 255.0f;
			}
			if (green > 255.0f) {
				green = 255.0f;
			}
			if (blue > 255.0f) {
				blue = 255.0f;
			}
			if (alpha > gdAlphaMax) {
				alpha = gdAlphaMax;
			}
			out_row[3*x] = red;
			out_row[3*x+1] = green;
			out_row[3*x+2] = blue;
		}
		jpeg_write_scanlines(out_cinfo, &out_row, 1);
		/* Forcibly stop, otherwise we get the error 'Application transferred too many scanlines' */
		if( in_dinfo->output_scanline >= in_dinfo->output_height ) break;
 	}

	unsigned int tmp;
	
	/* Finish reading the rest of the image, otherwise we get the error 'Application transferred too few scanlines' */
	if( in_dinfo->output_scanline != in_dinfo->image_height ) {
		JSAMPROW tmprow = malloc(in_dinfo->image_width * 3);		
		for( tmp = in_dinfo->output_scanline; tmp < in_dinfo->image_height; tmp++ ) {
			jpeg_read_scanlines(in_dinfo, &tmprow, 1);
		}
		free(tmprow);
	}
	
	/* Free up all the buffers used */
	for( tmp = 0; tmp < in_dinfo->image_height; tmp++ ) {
		if( buffer[tmp] ) {
			free( buffer[tmp] );
			buffer[tmp] = NULL;
		}
	}

	free(buffer);
	free(out_row);
}

static void
print_usage( char *argv0 ) {
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s <options>\n", basename(argv0));
	fprintf(stderr, "  -in     <file> : Input image filename\n");
	fprintf(stderr, "  -out    <file> : Output image filename\n");
	fprintf(stderr, "  -width   <int> : Maximum output width\n");
	fprintf(stderr, "  -height  <int> : Maximum output height\n");
	fprintf(stderr, "  -mode  <which> : Resize mode, scale/scalefit/crop/scaleaspect - default: scale\n");
	fprintf(stderr, "  -quality <int> : Output JPEG quality\n");
	fprintf(stderr, "  -debug         : Enable debug output\n");
	fprintf(stderr, "-crop <x:y:x2:y2>: Crop area for source image\n");
	fprintf(stderr, "\n");
}

void
irz_set_mode( irz_config_t *cfg, irz_mode_t mode ) {
	cfg->mode = mode;
}

bool
irz_set_infile( irz_config_t *cfg, const char *in_file ) {
	if( ! in_file || ! strlen(in_file) ) {
		irz_log(cfg, IRZ_ERROR, "missing or empty input file\n");
		return false;
	}
	cfg->in_file = in_file;
	return true;
}

bool
irz_set_outfile( irz_config_t *cfg, const char *out_file ) {
	if( ! out_file || ! strlen(out_file) ) {
		irz_log(cfg, IRZ_ERROR, "missing or empty output file\n");
		return false;
	}
	cfg->out_file = out_file;
	return true;
}

void
irz_set_debug( irz_config_t *cfg, bool debug_enabled ) {
	cfg->debug = debug_enabled;
}

void
irz_set_quality( irz_config_t *cfg, int out_quality ) {
	if( ! out_quality ) {
		cfg->out_quality = 75;
	}
	else if( out_quality < 1 || out_quality > 99 ) {
		irz_log(cfg, IRZ_WARN, "quality must be between 1 and 99\n");
		cfg->out_quality = 75;
	}
}

bool
irz_set_crop( irz_config_t *cfg, int crop_x1, int crop_y1, int crop_x2, int crop_y2 ) {
	if( crop_x1 < 0 || crop_y1 < 0 ) {
		irz_log(cfg, IRZ_ERROR, "crop_x1 and crop_y1 must be 0 or more");
		return false;
	}
	
	if( crop_x2 < crop_x1 ) {
		irz_log(cfg, IRZ_ERROR, "crop_x2 must be less than crop_x1");
		return false;
	}
	
	if( crop_y2 < crop_y1 ) {
		irz_log(cfg, IRZ_ERROR, "crop_y2 must be less than crop_y1");
		return false;
	}
	
	cfg->mode = MODE_CROPMANUAL;
	cfg->crop_x = crop_x1;
	cfg->crop_y = crop_y1;
	cfg->crop_x2 = crop_x2;
	cfg->crop_y2 = crop_y2;
	return true;
}

bool
irz_set_cropstring( irz_config_t *cfg, const char *cropstring ) {
	uint32_t crop_x1, crop_y1, crop_x2, crop_y2;
	
	if( sscanf(cropstring, "%u:%u:%u:%u", &crop_x1, &crop_y1, &crop_x2, &crop_y2) != 4 ) {
		irz_log(cfg, IRZ_ERROR, "invalid crop parameters, must be specified as 'x:y:x2:y2'\n");
		return false;
	}
	
	irz_set_crop(cfg, crop_x1, crop_y1, crop_x2, crop_y2);
	return true;
}

bool
irz_set_width( irz_config_t *cfg, int width ) {
	if( width < 0 ) {
		irz_log(cfg, IRZ_ERROR, "width must me 10 or more");
		return false;
	}
	
	cfg->out_width = width;
	return true;
}

bool
irz_set_height( irz_config_t *cfg, int height ) {
	if( height < 0 ) {
		irz_log(cfg, IRZ_ERROR, "height must be 10 or more");
		return false;
	}
	
	cfg->out_height = height;
	return true;
}


irz_config_t *
parse_options( int argc, char** argv ) {
	irz_config_t *config;
	int option;
	int missing_opts = 0;
	static const struct option options[] = {
		{ "in", 1, NULL, 'i' },
		{ "out", 1, NULL, 'o' },
		{ "quality", 1, NULL, 'q' }, 
		{ "mode", 1, NULL, 'm' },
		{ "debug", 0, NULL, 'd' },
		{ "width", 1, NULL, 'w' },
		{ "height", 1, NULL, 'h' },
		{ "crop", 1, NULL, 'c' },
		{NULL, 0, NULL, 0}
	};
	
	config = irz_new();
		
	while( 1 ) {
		option = getopt_long_only(argc, argv, "i:o:q:m:w:h: ", options, NULL);
		if( option == -1 ) break;
		switch(option) {
		case 'i':
			irz_set_infile(config, optarg);
			break;
	
		case 'o':
			irz_set_outfile(config, optarg);
			break;

		case 'd':
			irz_set_debug(config, true);
			break;

		case 'q':
			irz_set_quality(config, atoi(optarg));
			break;
			
		case 'w':
			irz_set_width(config, atoi(optarg));
			break;
			
		case 'm':
			if( strcasecmp(optarg,"crop") == 0 ) irz_set_mode(config, MODE_CROP);
			else if( strcasecmp(optarg,"scalefit") == 0 ) irz_set_mode(config, MODE_SCALEFIT);
                        else if( strcasecmp(optarg,"scaleaspect") == 0 ) irz_set_mode(config, MODE_SCALEASPECT);
			else {
				irz_log(config, IRZ_ERROR, "Unknown mode '%s'", optarg);
			}
			break;
		
		case 'c':	
			irz_set_cropstring(config, optarg);		
			break;
		
		case 'h':
			irz_set_height(config, atoi(optarg));
			break;
		}
	}


	if( config->mode == MODE_SCALE ) {
            if( config->out_width == 0 && config->out_height == 0 ) {
                    irz_log(config, IRZ_ERROR, "must specify either width or height when scaling an image\n");
                    missing_opts++;
            }
	}
	else if ( config->mode == MODE_SCALEASPECT) {
        	if( config->out_width < 1 && config->out_height < 1 ) {
                	irz_log(config, IRZ_ERROR, "must specify either width or height when scaling an image by aspect ratio\n");
                	missing_opts++;
        	}
    	}
	else {
		if( config->out_width == 0 ) {
			irz_log(config, IRZ_ERROR, "must specify output width when cropping an image\n");
			missing_opts++;
		}
		
		if( config->out_height == 0 ) {
			irz_log(config, IRZ_ERROR, "must specify output height when cropping an image\n");
			missing_opts++;
		}
	}
	
	if( ! missing_opts && config->out_width != 0 && config->out_width < 10 ) {
		irz_log(config, IRZ_ERROR, "missing or invalid output width, must be greater than 10\n");
		missing_opts++;
	}
	
	if( ! missing_opts && config->out_height != 0 && config->out_height < 10 ) {
		irz_log(config, IRZ_ERROR, "missing or invalid output height, must be greater than 10\n");
		missing_opts++;
	}
	
	if( missing_opts ) {
		print_usage(argv[0]);
		free(config);
		config = NULL;
	}
	
	return config;
}

const char *
irz_error_category(irz_error_t level) {
	if( level > IRZ_ERROR ) return "error";
	if( level > IRZ_WARN ) return "warn";
	if( level > IRZ_INFO ) return "info";
	if( level > IRZ_DEBUG ) return "debug";
	return "unknown";
}

bool
irz_logger_stdio( irz_config_t *cfg, irz_error_t level, char *fmt, va_list arg ) {
	assert( cfg->logger_cfg != NULL );
	fprintf((FILE*)cfg->logger_cfg, " [%s]: ", irz_error_category(level));
	vfprintf((FILE*)cfg->logger_cfg, fmt, arg);
	fprintf((FILE*)cfg->logger_cfg, "\n");
	return true;
}

int
main( int argc, char **argv ) {	
	irz_config_t *config;
	char *temp_out = tmpnam(NULL);

	FILE *in_fh = NULL;
	FILE *out_fh = NULL;
	
	config = parse_options(argc, argv);
	if( config == NULL ) {
		return EXIT_FAILURE+9;
	}
	
	config->logger = irz_logger_stdio;
	config->logger_cfg = stdout;
	
	in_fh = fopen(config->in_file,"r");
	if( ! in_fh ) {
		char buf[200];
		strerror_r(errno, buf, sizeof(buf));
		irz_log(config, IRZ_ERROR, "Cannot open in-file '%s': %s", config->in_file, buf);
		return EXIT_FAILURE+11;
	}
	
	struct jpeg_compress_struct out_cinfo;
	struct jpeg_decompress_struct in_dinfo;
	struct jpeg_error_mgr       jerr;		
	
	setup_in(&in_dinfo, &jerr, in_fh);

	if( config->debug ) {
		irz_log(config, IRZ_DEBUG, "Image Width: %ipx\n", (int)in_dinfo.image_width);
	        irz_log(config, IRZ_DEBUG, "Image Height: %ipx\n", (int)in_dinfo.image_height);
	        irz_log(config, IRZ_DEBUG, "Tmp Loc: %s\n", temp_out);
	}

	if ( config->out_width != 0 ) {
        	if ( (int)in_dinfo.image_width < (int)config->out_width ) {
            		irz_log(config, IRZ_WARN, "specified width is larger than original image, reducing to %ipx.\n", (int)in_dinfo.image_width);
            		config->out_width = (double)in_dinfo.image_width;
        	}
    	}

    	if ( config->out_height != 0 ) {
        	if ( (int)in_dinfo.image_height < (int)config->out_height ) {
            		irz_log(config, IRZ_WARN, "specified height is larger than original image, reducing to %ipx.\n", (int)in_dinfo.image_height);
            		config->out_height = (double)in_dinfo.image_height;
        	}
    	}
	
	/* Auto-scale width or height if either was not specified */
	if( config->mode == MODE_SCALE ) {		
		if( config->out_height == 0 ) {
			double ratio = (double)config->out_width / (double)in_dinfo.image_width;
			config->out_height = ((double)in_dinfo.image_height * ratio);
		}
		else if( config->out_width == 0 ) {
			double ratio = (double)config->out_height / (double)in_dinfo.image_height;
			config->out_width = ((double)in_dinfo.image_width * ratio);
		}
	}
    	else if( config->mode == MODE_SCALEASPECT ) {
        	double ratio;
        	unsigned int new_width = in_dinfo.image_width;
        	unsigned int new_height = in_dinfo.image_height;

        	if (in_dinfo.image_width > in_dinfo.image_height) {
            		ratio = ( (double)in_dinfo.image_width / (double)in_dinfo.image_height ) ;
        	}
		else {
            		ratio = ( (double)in_dinfo.image_height / (double)in_dinfo.image_width ) ;
        	}

		if( config->debug ) {
        		irz_log(config, IRZ_DEBUG, "Ratio: %f\n", ratio);
		}

        	if (in_dinfo.image_width < in_dinfo.image_height) {
            		if (new_width > config->out_width) {
                		new_width = config->out_width;
                		new_height = ( new_height - ( ( (double)in_dinfo.image_width - (double)config->out_width) * ratio ) );

				if( config->debug ) {
             	   			irz_log(config, IRZ_DEBUG, "aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            		}

            		if (new_height > config->out_height) {
                		new_width = ( new_width - ( ( (double)new_height - (double)config->out_height) / ratio ) );
                		new_height = config->out_height;

				if( config->debug ) {
             	   			irz_log(config, IRZ_DEBUG, "aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            		}
        	}
		else {
            		if (new_width > config->out_width) {
                		new_width = config->out_width;
                		new_height = ( new_height - ( ( (double)in_dinfo.image_width - (double)config->out_width) / ratio ) );

				if( config->debug ) {
             	   			irz_log(config, IRZ_DEBUG, "aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            		}

            		if (new_height > config->out_height) {
                		new_width = ( new_width - ( ( (double)new_height - (double)config->out_height) * ratio ) );
                		new_height = config->out_height;

				if( config->debug ) {
              	  			irz_log(config, IRZ_DEBUG, "aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            		}
        	}

        	config->out_width = new_width;
        	config->out_height = new_height;
	}
	else if( config->mode == MODE_SCALEFIT ) {
		if( in_dinfo.image_width > config->out_width ) {
			double ratio = (double)config->out_width / (double)in_dinfo.image_width;
			config->out_height = ((double)in_dinfo.image_height * ratio);
			config->out_width = ((double)in_dinfo.image_width * ratio);
		}
		if( in_dinfo.image_height > config->out_height ) {
			double ratio = (double)config->out_height / (double)in_dinfo.image_height;
			config->out_height = ((double)in_dinfo.image_height * ratio);
			config->out_width = ((double)in_dinfo.image_width * ratio);
		}
	}
	else if( config->mode == MODE_CROPMANUAL ) {
		if( (config->crop_x2 - config->crop_x) < 10 ) {
			irz_log(config, IRZ_ERROR, "source crop width too small\n");
			return EXIT_FAILURE+10;
		}
		else if( (config->crop_y2 - config->crop_y) < 10 ) {
			irz_log(config, IRZ_ERROR, "source crop height too small\n");
			return EXIT_FAILURE+10;
		}
		else if( config->crop_x2 > in_dinfo.image_width || config->crop_y2 > in_dinfo.image_height ) {
			irz_log(config, IRZ_ERROR, "source crop area outside of image dimension\n");
			return EXIT_FAILURE+10;
		}
	}

	if( config->debug ) {
        	irz_log(config, IRZ_DEBUG, "Out Width: %upx\n", config->out_width);
        	irz_log(config, IRZ_DEBUG, "Out Height: %upx\n", config->out_height);
	}
	
	out_fh = fopen(temp_out,"wb");
	if( ! out_fh ) {
		char buf[200];
		strerror_r(errno, buf, sizeof(buf));
		irz_log(config, IRZ_ERROR, "Cannot open output file '%s': %s", temp_out, buf);
		return EXIT_FAILURE+12;
	}

	setup_out(&out_cinfo, &jerr, out_fh, config->out_width, config->out_height, config->out_quality);

	int row_stride = in_dinfo.output_width * in_dinfo.output_components;
	JSAMPARRAY buffer;
	buffer = (in_dinfo.mem->alloc_sarray)((j_common_ptr) &in_dinfo, JPOOL_IMAGE, row_stride, 1);
		
	do_resize(&out_cinfo, &in_dinfo, config);

	jpeg_finish_decompress(&in_dinfo);
	jpeg_destroy_decompress(&in_dinfo);
	
	jpeg_finish_compress(&out_cinfo);
	jpeg_destroy_compress(&out_cinfo);
	
	fclose(in_fh);
	fclose(out_fh);
	
	/* XXX: needs to be refactored as rename() won't work across filesystems
	 * temp_out should be in the same directory as out_file */
	if( rename( temp_out, config->out_file ) != 0 ) {
		char buf[200];
		strerror_r(errno, buf, sizeof(buf));
		irz_log(config, IRZ_ERROR, "Cannot rename temporary file '%s' to real file '%s': %s", temp_out, config->out_file, buf);
		return EXIT_FAILURE+13;
	}
	
	free(config);	
	return EXIT_SUCCESS;
}
