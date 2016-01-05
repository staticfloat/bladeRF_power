#include <stdio.h>
#include <libbladeRF.h>
#include "conversions.h"
#include <time.h>

// Some useful define's
#define ERROR(x...) fprintf(stderr, x)
#define LOG(x...)  if( opts.verbosity > 0 ) { fprintf(stderr, x); }
#define INFO(x...) if( opts.verbosity > 1 ) { fprintf(stderr, x); }

#ifndef MAX
#define MAX(x, y) ((x)  > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#endif
#define msdiff(a, b) ((a.tv_sec - b.tv_sec)*1000 + (a.tv_usec - b.tv_usec)/1000)


bool gen_window(const char * window_name, double * window, unsigned int len);

// Time.... time makes fools of us all
void time2str(struct timeval &tv, char * out);


// Suffix stuffage
#define NUM_FREQ_SUFFIXES 6
extern const struct numeric_suffix freq_suffixes[NUM_FREQ_SUFFIXES];
#define NUM_TIME_SUFFIXES 5
extern const struct numeric_suffix time_suffixes[NUM_TIME_SUFFIXES];

// Here are some things that should go back into conversions.{c,h} I think...
int double2str_suffix(char * out, double val, const struct numeric_suffix suffixes[],
                      size_t num_suffixes);

unsigned char bladerf_lna_gain_to_db(bladerf_lna_gain lna, bool *ok);
bladerf_lna_gain bladerf_db_to_lna_gain(unsigned char db, bool *ok);
