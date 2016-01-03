#include <stdio.h>
#include <stdbool.h>
#include "conversions.h"

#define ERROR(x...) fprintf(stderr, x)
#define LOG(x...)  if( opts.verbosity > 0 ) { fprintf(stderr, x); }
#define INFO(x...) if( opts.verbosity > 1 ) { fprintf(stderr, x); }


struct opts_struct {
    // Our verbosity level
    int verbosity;

    // The frequencies we will tune to
    unsigned int * freqs;

    // The number of frequencies contained within freqs
    unsigned short num_freqs;

    // Whether the first view location in freqs is a lower sideband
    bool first_freq_lower_sideband;

    // The samplerate/bandwidth at which we will be capturing data
    unsigned int samplerate;

    // How much of each spectrum we use at a time
    double filter_margin;

    // The amount of data we need to capture before running an FFT on
    unsigned int fft_len;

    // Gains of amplifiers within the bladeRF
    bladerf_lna_gain lna;
    unsigned char rxvga1, rxvga2;

    // Internal buffer settings (we don't have a way to set these right now)
    unsigned int num_buffers;
    unsigned int buffer_size;
    unsigned int num_transfers;
    unsigned int timeout_ms;

    // File output handle
    int file;

    // bladeRF device
    struct bladerf *dev;
    const char * devstr;
};
extern struct opts_struct opts;

void parse_options(int argc, char ** argv);




#define NUM_FREQ_SUFFIXES 6
extern const struct numeric_suffix freq_suffixes[NUM_FREQ_SUFFIXES];
// Here're some things that should go back into conversions.{c,h} I think...
void double2str_suffix(char * out, double val, const struct numeric_suffix suffixes[],
                       size_t num_suffixes);

unsigned char bladerf_lna_gain_to_db(bladerf_lna_gain lna, bool *ok);
bladerf_lna_gain bladerf_db_to_lna_gain(unsigned int db, bool *ok);
