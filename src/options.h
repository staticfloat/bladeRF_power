#include <libbladeRF.h>

struct opts_struct {
    // Our verbosity level
    int verbosity;

    // The start/end frequencies for our capture, as well as the bin width
    unsigned int start_freq, end_freq;
    double bin_width;

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

    // The temporal window we will apply to each buffer
    char * window_name;
    double * window;

    // The number of bins we are going to end up with in our FFT
    unsigned int fft_len;

    // The number of buffers of fft_len we integrate together to fight noise
    unsigned int num_integrations;

    // Number of milliseconds to run for
    unsigned int exit_timer;

    // filter margin * bandwidth/2, rounded due to fft_len
    double fmbw2;

    // Gains of amplifiers within the bladeRF
    bladerf_lna_gain lna;
    unsigned char rxvga1, rxvga2;

    // Internal buffer settings (we don't have a way to set these right now)
    unsigned int num_buffers;
    unsigned int buffer_size;
    unsigned int num_transfers;
    unsigned int timeout_ms;

    // Number of analysis threads
    unsigned int num_threads;

    // File output handle
    int file;

    // bladeRF device name
    char * devstr;
};
extern struct opts_struct opts;

void parse_options(int argc, char ** argv);
void cleanup_options();
