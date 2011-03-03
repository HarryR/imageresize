#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>

/*
Copyright (c) 2009 ECMP Ltd.
Copyright (c) 2010 Harry Roberts.
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

enum irz_mode {
	MODE_SCALE,
	MODE_SCALEASPECT,
	MODE_SCALEFIT,
	MODE_CROP,
	MODE_CROPMANUAL
};

struct irz_config {
	char *in_file;
	char *out_file;
	int mode;	
	int out_quality;
    int debug;
	unsigned int out_width;
	unsigned int out_height;
	
	uint32_t crop_x;
	uint32_t crop_y;
	uint32_t crop_x2;
	uint32_t crop_y2;
};
typedef struct irz_config irz_config_t;

static void
setup_out(struct jpeg_compress_struct *out_cinfo, struct jpeg_error_mgr *jerr,
					FILE* out_fh, int out_width, int out_height, int out_quality)
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
	
//	in_dinfo->out_color_space = JCS_RGB;
	jpeg_stdio_src( in_dinfo, in_fh );
	jpeg_read_header( in_dinfo, TRUE );
	jpeg_start_decompress(in_dinfo);
}

#define gdAlphaMax 127
/* 2.0.10: cast instead of floor() yields 35% performance improvement. Thanks to John Buckman. */
#define floor_cast(exp) ((long) exp)

static void
do_resize( struct jpeg_compress_struct *out_cinfo,
					 struct jpeg_decompress_struct *in_dinfo,
					 irz_config_t* config)
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
		int src_min = srcW < srcH ? srcW : srcH;
		int dst_max = dstW > dstH ? dstW : dstH;
		double ratio = src_min / dst_max;
		int width = ratio * (double)dstW;
		int height = ratio * (double)dstH;
		int offset_x = (srcW / 2) - (width / 2);
		int offset_y = (srcH / 2) - (height / 2);
		
		srcW = width;
		srcH = height;
		srcX = offset_x;
		srcY = offset_y;
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
	
	// Pre-read scanlines from the image until we're at the point where it matters
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
                            } else if (sy == floorf(sy2)) {
                                    yportion = sy2 - floor_cast(sy2);
                            } else {
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
                                    } else if (sx == floorf(sx2)) {
                                            xportion = sx2 - floor_cast(sx2);
                                    } else {
                                            xportion = 1.0f;
                                    }
                                    pcontribution = xportion * yportion;

									//
									JSAMPLE* p = buffer[ (int)sy + srcY ] + ((int)(sx + srcX) * in_dinfo->num_components);
									alpha_factor = gdAlphaMax * pcontribution;
				
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
									//

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
		// Forcibly stop, otherwise we get the error 'Application transferred too many scanlines'
		if( in_dinfo->output_scanline >= in_dinfo->output_height ) break;
 	}

	unsigned int tmp;
	
	// Finish reading the rest of the image, otherwise we get the error 'Application transferred too few scanlines'
	if( in_dinfo->output_scanline != in_dinfo->image_height ) {
		JSAMPROW tmprow = malloc(in_dinfo->image_width * 3);		
		for( tmp = in_dinfo->output_scanline; tmp < in_dinfo->image_height; tmp++ ) {
			jpeg_read_scanlines(in_dinfo, &tmprow, 1);
		}
		free(tmprow);
	}
	
	// Free up all the buffers used
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
	
	config = malloc(sizeof(irz_config_t));
	memset(config, 0, sizeof(irz_config_t));
	config->mode = MODE_SCALE;
	
	while( 1 ) {
		option = getopt_long_only(argc, argv, "i:o:q:m:w:h: ", options, NULL);
		if( option == -1 ) break;
		switch(option) {
		case 'i':
			config->in_file = optarg;
			break;
	
		case 'o':
			config->out_file = optarg;
			break;

		case 'd':
			config->debug = 1;
			break;

		case 'q':
			config->out_quality = atoi(optarg);
			break;
			
		case 'w':
			config->out_width = atoi(optarg);
			break;
			
		case 'm':
			if( strcmp(optarg,"crop") == 0 ) config->mode = MODE_CROP;
			if( strcmp(optarg,"scalefit") == 0 ) config->mode = MODE_SCALEFIT;
                        if( strcmp(optarg,"scaleaspect") == 0 ) config->mode = MODE_SCALEASPECT;
			break;
		
		case 'c':
			config->mode = MODE_CROPMANUAL;
			if( sscanf(optarg, "%u:%u:%u:%u", &config->crop_x, &config->crop_y, &config->crop_x2, &config->crop_y2) != 4 ) {
				fprintf(stderr, "Error: invalid crop parameters, must be specified as 'x:y:x2:y2'\n");
				missing_opts++;
			}
			break;
		
		case 'h':
			config->out_height = atoi(optarg);
			break;
		}
	}
	
	if( ! config->in_file ) {
		fprintf(stderr, "Error: missing input file\n");
		missing_opts++;
	}
	
	if( ! config->out_file ) {
		fprintf(stderr, "Error: missing output file\n");
		missing_opts++;
	}
	
	if( ! config->out_quality ) {
		config->out_quality = 75;
	}
	else if( config->out_quality < 1 || config->out_quality > 99 ) {
		fprintf(stderr, " [*] Warning: quality must be between 1 and 99\n");
		config->out_quality = 75;
	}

	if( config->mode == MODE_SCALE ) {
            if( config->out_width == 0 && config->out_height == 0 ) {
                    fprintf(stderr, "Error: must specify either width or height when scaling an image\n");
                    missing_opts++;
            }
	}
	else if ( config->mode == MODE_SCALEASPECT) {
        if( config->out_width < 1 && config->out_height < 1 ) {
                fprintf(stderr, "Error: must specify either width or height when scaling an image by aspect ratio\n");
                missing_opts++;
        }
    }
	else { // if( config->mode == MODE_CROP ) {
		if( config->out_width == 0 ) {
			fprintf(stderr, "Error: must specify output width when cropping an image\n");
			missing_opts++;
		}
		
		if( config->out_height == 0 ) {
			fprintf(stderr, "Error: must specify output height when cropping an image\n");
			missing_opts++;
		}
	}
	
	if( ! missing_opts && config->out_width != 0 && config->out_width < 10 ) {
		fprintf(stderr, "Error: missing or invalid output width, must be greater than 10\n");
		missing_opts++;
	}
	
	if( ! missing_opts && config->out_height != 0 && config->out_height < 10 ) {
		fprintf(stderr, "Error: missing or invalid output height, must be greater than 10\n");
		missing_opts++;
	}
	
	if( missing_opts ) {
		print_usage(argv[0]);
		free(config);
		config = NULL;
	}
	
	return config;
}

int
main( int argc, char **argv ) {	
	irz_config_t *config;
	char *temp_out = tmpnam(NULL);

	FILE *in_fh = NULL;
	FILE *out_fh = NULL;
	
	config = parse_options(argc, argv);
	if( config == NULL ) {
		return 9;
	}
	
	/*
	if( config->mode == MODE_CROP ) {
		fprintf(stderr, "Error: crop mode is currently unsupported!\n");
		return 10;
	}
	*/
	
	in_fh = fopen(config->in_file,"r");
	if( ! in_fh ) {
		perror("in:file - fopen");
		return 11;
	}
	
	struct jpeg_compress_struct out_cinfo;
	struct jpeg_decompress_struct in_dinfo;
	struct jpeg_error_mgr       jerr;		
	
	setup_in(&in_dinfo, &jerr, in_fh);

	if( config->debug ) {
        printf(" [*] Image Width: %ipx\n", (int)in_dinfo.image_width);
        printf(" [*] Image Height: %ipx\n", (int)in_dinfo.image_height);
        printf(" [*] Tmp Loc: %s\n", temp_out);
	}

    if ( config->out_width != 0 ) {
        if ( (int)in_dinfo.image_width < (int)config->out_width ) {
            fprintf(stderr, " [*] Warning: specified width is larger than original image, reducing to %ipx.\n", (int)in_dinfo.image_width);
            config->out_width = (double)in_dinfo.image_width;
        }
    }

    if ( config->out_height != 0 ) {
        if ( (int)in_dinfo.image_height < (int)config->out_height ) {
            fprintf(stderr, " [*] Warning: specified height is larger than original image, reducing to %ipx.\n", (int)in_dinfo.image_height);
            config->out_height = (double)in_dinfo.image_height;
        }
    }
	
	// Auto-scale width or height if either was not specified
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
        } else {
            ratio = ( (double)in_dinfo.image_height / (double)in_dinfo.image_width ) ;
        }

		if( config->debug ) {
        	fprintf(stdout, " [*] Ratio: %f\n", ratio);
		}

        if (in_dinfo.image_width < in_dinfo.image_height) {
            if (new_width > config->out_width) {
                new_width = config->out_width;
                new_height = ( new_height - ( ( (double)in_dinfo.image_width - (double)config->out_width) * ratio ) );

				if( config->debug ) {
             	   fprintf(stdout, " [-] aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            }

            if (new_height > config->out_height) {
                new_width = ( new_width - ( ( (double)new_height - (double)config->out_height) / ratio ) );
                new_height = config->out_height;

				if( config->debug ) {
             	   fprintf(stdout, " [-] aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            }
        } else {
            if (new_width > config->out_width) {
                new_width = config->out_width;
                new_height = ( new_height - ( ( (double)in_dinfo.image_width - (double)config->out_width) / ratio ) );

				if( config->debug ) {
             	   fprintf(stdout, " [*] aspect changed: width: %u / height: %u\n", new_width, new_height );
				}
            }

            if (new_height > config->out_height) {
                new_width = ( new_width - ( ( (double)new_height - (double)config->out_height) * ratio ) );
                new_height = config->out_height;

				if( config->debug ) {
              	  fprintf(stdout, " [*] aspect changed: width: %u / height: %u\n", new_width, new_height );
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
			fprintf(stderr, "Error: source crop width too small\n");
			return 10;
		}
		else if( (config->crop_y2 - config->crop_y) < 10 ) {
			fprintf(stderr, "Error: source crop height too small\n");
			return 10;
		}
		else if( config->crop_x2 > in_dinfo.image_width || config->crop_y2 > in_dinfo.image_height ) {
			fprintf(stderr, "Error: source crop area outside of image dimension\n");
			return 10;
		}
	}

	if( config->debug ) {
        fprintf(stdout, " [*] Out Width: %upx\n", config->out_width);
        fprintf(stdout, " [*] Out Height: %upx\n", config->out_height);
	}
	
	//out_fh = fopen(config->out_file,"wb");
	out_fh = fopen(temp_out,"wb");
	if( ! out_fh ) {
		perror("out:file - fopen");
		return 12;
	}

	setup_out(&out_cinfo, &jerr, out_fh, config->out_width, config->out_height, config->out_quality);

	int row_stride = in_dinfo.output_width * in_dinfo.output_components;
	JSAMPARRAY buffer;
	buffer = (in_dinfo.mem->alloc_sarray)
		((j_common_ptr) &in_dinfo, JPOOL_IMAGE, row_stride, 1);
		
	do_resize(&out_cinfo, &in_dinfo, config);

	jpeg_finish_decompress(&in_dinfo);
	jpeg_destroy_decompress(&in_dinfo);
	
	jpeg_finish_compress(&out_cinfo);
	jpeg_destroy_compress(&out_cinfo);
	
	fclose(in_fh);
	fclose(out_fh);
	
	if( rename( temp_out, config->out_file ) != 0 ) {
		perror("Error: Cannot rename temporary file to real file - ");
		return 13;
	}
	
	free(config);
	
	return 0;
}
