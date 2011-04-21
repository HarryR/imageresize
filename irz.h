#ifndef IRZ_H_
#define IRZ_H_

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum _irz_mode_e {
	MODE_NONE = 0,
	MODE_SCALE,
	MODE_SCALEASPECT,
	MODE_SCALEFIT,
	MODE_CROP,
	MODE_CROPMANUAL
} irz_mode_t;

typedef enum _irz_error_e {
	IRZ_DEBUG = 0,
	IRZ_INFO = 1000,
	IRZ_WARN = 2000,
	IRZ_ERROR = 3000
} irz_error_t;

struct irz_config;
typedef bool (*irz_logger_cb)(struct irz_config *cfg, irz_error_t level, char *fmt, va_list arg);

/**
 * Textual name for a logger level
 * @param level Which level
 */
const char *irz_log_threshold(irz_error_t level);

struct irz_config {
	const char *in_file;
	const char *out_file;
	irz_mode_t mode;	
	int out_quality;	
	unsigned int out_width;
	unsigned int out_height;
	
	unsigned int crop_x;
	unsigned int crop_y;
	unsigned int crop_x2;
	unsigned int crop_y2;
	
	irz_logger_cb logger;
	void *logger_cfg;
	irz_error_t logger_threshold;
	
	bool debug;	
};
typedef struct irz_config irz_config_t;

irz_config_t *irz_new(void);
void irz_init(irz_config_t *cfg);
void irz_free(irz_config_t *cfg);

/**
 * Call the configured logger
 */
bool irz_log(struct irz_config *cfg, irz_error_t level, char *fmt, ...);

/**
 * Name of which category the error is
 */
const char *irz_error_category(irz_error_t level);

bool irz_set_infile( irz_config_t *cfg, const char *in_file );
bool irz_set_outfile( irz_config_t *cfg, const char *out_file );
bool irz_set_crop( irz_config_t *cfg, int crop_x1, int crop_y1, int crop_x2, int crop_y2 );
void irz_set_mode( irz_config_t *cfg, irz_mode_t mode );
void irz_set_debug( irz_config_t *cfg, bool debug_enabled );
void irz_set_quality( irz_config_t *cfg, int out_quality );

#endif
