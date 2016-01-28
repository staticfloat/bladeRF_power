// Include libbladeRF conversion code
#include "options.h"
#include "util.h"
#include "conversions.h"
#include <libbladeRF.h>
#include <getopt.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <limits.h>

// Instantiate our options struct
struct opts_struct opts;

void version()
{
    printf("bladeRF_power v0.0.1\n");
}

void usage()
{
    version();
    printf("Usage:\n");
    printf("  bladeRF_power <lower:upper:bin_width> [options]\n");
    printf("  bladeRF_power (-h | --help)\n");
    printf("  bladeRF_power --version\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  <lower:upper:bin_width>  Frequency sweep parameters, e.g. <900M:1.2G:10K>\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h --help                  Show this screen.\n");
    printf("  -V --version               Show version.\n");
    printf("  -v --verbose               Raise verbosity level (can be repeated)\n");
    printf("  -f --file=<outfile>        Write to file `outfile` instead of stdout\n");
    printf("  -e --exit-timer=<et>       Set capture time (examples: \"5h\", \"30m\") [default: 0]\n");
    printf("  -i --integration-time=<t>  Time to integrate over to reduce noise in FFT.\n");
    printf("                             Supports time suffixes, [default: bin_width/bandwidth]\n");
    printf("  -b --bandwidth=<bw>        Capture bandwidth [default: BLADERF_BANDWIDTH_MAX].\n");
    printf("  -M --filter-margin=<fm>    Anti-aliasing filter margin [default: .55]. This value\n");
    printf("                             is combined with bandwidth to view only a portion of\n");
    printf("                             the captured signal to combat leaky anti-aliasing\n");
    printf("                             filters. Actual useful signal bandwidth is fm*bw/2.\n");
    printf("  -W --window-type=<wt>      Set temporal windowing function [default: hamming]\n");
    printf("  -g --lna-gain=<g>          Set LNA gain either as numeric dB value (0, %d, %d) or\n", BLADERF_LNA_GAIN_MID_DB, BLADERF_LNA_GAIN_MAX_DB);
    printf("                             as symbolic (bypass, min, max). [default: max]\n");
    printf("  -o --rx-vga1=<g>           Set rxvga1 gain either as numeric dB value [%d...%d] or\n", BLADERF_RXVGA1_GAIN_MIN, BLADERF_RXVGA1_GAIN_MAX);
    printf("                             as symbolic (min, max).  [default: min]\n");
    printf("  -w --rx-vga2=<g>           Set rxvga2 gain either as numeric dB value [%d...%d] or\n", BLADERF_RXVGA2_GAIN_MIN, BLADERF_RXVGA2_GAIN_MAX);
    printf("                             as symbolic (min, max).  [default: min]\n");
    printf("  -d --device=<d>            Device identifier [default: ]\n");
    printf("  -T --threads=<t>           Set number of worker threads [default: 2]\n");
}

static const struct option longopts[] = {
    { "help",               no_argument,        0, 'h' },
    { "version",            no_argument,        0, 'V' },
    { "verbose",            no_argument,        0, 'v' },
    { "file",               required_argument,  0, 'f' },
    { "exit-timer",         required_argument,  0, 'e' },
    { "integration-time",   required_argument,  0, 'i' },
    { "bandwidth",          required_argument,  0, 'b' },
    { "filter-margin",      required_argument,  0, 'M' },
    { "window-type",        required_argument,  0, 'W' },
    { "lna-gain",           required_argument,  0, 'g' },
    { "rxvga1-gain",        required_argument,  0, 'o' },
    { "rxvga2-gain",        required_argument,  0, 'w' },
    { "device",             required_argument,  0, 'd' },
    { "threads",            required_argument,  0, 'T' },
    { 0,                    0,                  0,  0  },
};



bool parse_freq_range(char * freq_range, unsigned int * start_freq,
                      unsigned int * end_freq, unsigned int * bin_width)
{
    bool ok;
    const char * start_freq_str = freq_range;
    const char * end_freq_str = NULL;
    const char * bin_width_str = NULL;

    unsigned int freq_range_len = strlen(freq_range);
    for( int idx=0; idx<freq_range_len; ++idx ) {
        if( freq_range[idx] == ':' ) {
            freq_range[idx] = '\0';
            if( end_freq_str == NULL )
                end_freq_str = freq_range + idx + 1;
            else
                bin_width_str = freq_range + idx + 1;
        }
    }
    if( end_freq_str == NULL || bin_width_str == NULL )
        return false;

    *start_freq = str2uint_suffix(start_freq_str, BLADERF_FREQUENCY_MIN,
                                  BLADERF_FREQUENCY_MAX, freq_suffixes,
                                  NUM_FREQ_SUFFIXES, &ok);
    if( !ok )
        return false;
    *end_freq = str2uint_suffix(  end_freq_str, BLADERF_FREQUENCY_MIN,
                                  BLADERF_FREQUENCY_MAX, freq_suffixes,
                                  NUM_FREQ_SUFFIXES, &ok);
    if( !ok )
        return false;
    *bin_width = str2uint_suffix( bin_width_str, 1, opts.samplerate,
                                  freq_suffixes, NUM_FREQ_SUFFIXES, &ok);
    if( !ok )
        return false;
    return true;
}


/*
 Given frequency parameters, generates a list of frequencies and stores them
 into the "opts" struct's "freqs", "num_freqs", and "first_freq_lower_sideband"
 fields, denoting the center frequency of each tuning view, and whether we
 should pay attention to the lower sideband or upper sideband when tuning to the
 first frequency.
*/
void plan_frequencies(unsigned int start_freq, unsigned int end_freq,
                      float bin_width, unsigned int fmbw2)
{
    // How many frequencies will we need to tune to? (add fbmw2 - 1 on to the
    // total bandwidth to round up).
    opts.num_freqs = (end_freq - start_freq + fmbw2 - 1)/fmbw2;
    opts.freqs = (unsigned int *) malloc(sizeof(unsigned int)*opts.num_freqs);

    if( start_freq - bin_width >= BLADERF_FREQUENCY_MIN ) {
        // Put freqs[0] just below start_freq if we are not already at the
        // minimum frequency, and thus unable to tune below start_freq
        opts.freqs[0] = start_freq - (unsigned int)bin_width;
        opts.first_freq_lower_sideband = false;
    } else {
        // Otherwise, put center_freq just above start_freq + bandwidth/2 and
        // capture the lower sideband of this view
        opts.freqs[0] = start_freq + (unsigned int)fmbw2;
        opts.first_freq_lower_sideband = true;
    }

    // Fill in the rest of the center frequencies
    for( int idx=1; idx < opts.num_freqs; ++idx )
        opts.freqs[idx] = start_freq - ((unsigned int)bin_width) + idx*((unsigned int)fmbw2);
}



// Macro to set default values that are initialized to zero
#define DEFAULT(field, val) if( field == 0 ) { field = val; }
#define OPTSTR "hvVzf:i:e:b:M:W:T:g:o:w:d:"

void parse_options(int argc, char ** argv)
{
    // First thing we do is initialize the entire opts struct to zero
    memset(&opts, 0, sizeof(opts));

    // Declare some temporary variables
    bool ok;

    // Next, parse options into opts:
    int optidx;
    int c = getopt_long(argc, argv, OPTSTR, longopts, &optidx);
    do {
        switch(c) {
            case 'h':
                usage();
                exit(0);
                break;
            case 'v':
                opts.verbosity++;
                break;
            case 'V':
                version();
                exit(0);
                break;
            case 'f':
                opts.file = open(optarg, O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK, 0664);
                if( opts.file == -1 ) {
                    ERROR("Unable to open \"%s\" for writing!\n", optarg);
                    exit(1);
                }
                break;
            case 'e':
                opts.exit_timer = str2uint_suffix(optarg, 0, UINT_MAX, time_suffixes,
                                                  NUM_TIME_SUFFIXES, &ok);
                if( !ok ) {
                    ERROR("Invalid exit timer \"%s\"\n", optarg);
                    ERROR("Valid values given in milliseconds (ex: \"60000\")");
                    ERROR("or, equivalently, with units: (ex: \"60s\" or \"1m\")\n");
                    exit(1);
                }
                break;
            case 'i':
                opts.num_integrations = str2uint_suffix(optarg, 1, UINT_MAX, time_suffixes,
                                                        NUM_TIME_SUFFIXES, &ok);
                if( !ok ) {
                    ERROR("Invalid exit timer \"%s\"\n", optarg);
                    ERROR("Valid values given in milliseconds (ex: \"60000\")");
                    ERROR("or, equivalently, with units: (ex: \"60s\" or \"1m\")\n");
                    exit(1);
                }
                break;
            case 'b':
                opts.samplerate = str2uint_suffix(optarg, BLADERF_BANDWIDTH_MIN,
                                                  BLADERF_BANDWIDTH_MAX, freq_suffixes,
                                                  NUM_FREQ_SUFFIXES, &ok);
                if( !ok ) {
                    ERROR("Invalid bandwidth \"%s\"\n", optarg);
                    ERROR("Valid range: [%u, %u]\n", BLADERF_BANDWIDTH_MIN, BLADERF_BANDWIDTH_MAX);
                    exit(1);
                }
                break;
            case 'M':
                opts.filter_margin = str2double(optarg, .1, 1, &ok);
                if( !ok ) {
                    ERROR("Invalid filter margin \"%s\"\n", optarg);
                    ERROR("Valid range: [.1, 1]\n");
                    exit(1);
                }
                break;
            case 'W':
                opts.window_name = strdup(optarg);
                if( !gen_window(opts.window_name, NULL, 0) ) {
                    ERROR("Invalid window name \"%s\"\n", optarg);
                    ERROR("Valid values: \"rect\", \"hann\", \"hamming\"\n");
                    exit(1);
                }
                break;
            case 'g': {
                // Try to interpret it as an integer
                unsigned char db = str2uint(optarg, 0, BLADERF_LNA_GAIN_MAX_DB, &ok);
                if( ok ) {
                    opts.lna = bladerf_db_to_lna_gain(db, &ok);
                    if( !ok ) {
                        ERROR("Invalid numeric LNA gain \"%s\"\n", optarg);
                        ERROR("Valid values: [0, %d, %d]\n", BLADERF_LNA_GAIN_MID_DB, BLADERF_LNA_GAIN_MAX_DB);
                        exit(1);
                    }
                } else {
                    // If that fails, try interpreting it as a symbol:
                    if( str2lnagain(optarg, &opts.lna) == -1 ) {
                        ERROR("Unable to parse symbolic LNA gain \"%s\"\n", optarg);
                        ERROR("Valid values: [\"bypass\", \"mid\", \"max\"]\n");
                        exit(1);
                    }
                }
            }   break;
            case 'o':
                opts.rxvga1 = str2uint(optarg, BLADERF_RXVGA1_GAIN_MIN,
                                    BLADERF_RXVGA1_GAIN_MAX, &ok);
                if( !ok ) {
                    ERROR("Invalid RXVGA1 gain \"%s\"\n", optarg);
                    ERROR("Valid range: [%u, %u]\n", BLADERF_RXVGA1_GAIN_MIN, BLADERF_RXVGA1_GAIN_MAX);
                    exit(1);
                }
                break;
            case 'w':
                opts.rxvga2 = str2uint(optarg, BLADERF_RXVGA2_GAIN_MIN,
                                    BLADERF_RXVGA2_GAIN_MAX, &ok);
                if( !ok ) {
                    ERROR("Invalid RXVGA2 gain \"%s\"\n", optarg);
                    ERROR("Valid range: [%u, %u]\n", BLADERF_RXVGA2_GAIN_MIN, BLADERF_RXVGA2_GAIN_MAX);
                    exit(1);
                }
                break;
            case 'd':
                opts.devstr = strdup(optarg);
                break;
            case 'T':
                opts.num_threads = str2uint(optarg, 0, 128, &ok);
                if( !ok ) {
                    ERROR("Invalid number of threads \"%s\"\n", optarg);
                    ERROR("Valid range: [0, 128]\n");
                    exit(1);
                }
                break;
        }

        c = getopt_long(argc, argv, OPTSTR, longopts, &optidx);
    } while (c != -1);

    // Set defaults for everything that didn't get an explicit value:
    DEFAULT(opts.verbosity, 0);
    DEFAULT(opts.samplerate, BLADERF_BANDWIDTH_MAX);
    DEFAULT(opts.filter_margin, 0.55);
    DEFAULT(opts.window_name, strdup("hamming"));
    DEFAULT(opts.lna, BLADERF_LNA_GAIN_MAX);
    DEFAULT(opts.rxvga1, BLADERF_RXVGA1_GAIN_MIN);
    DEFAULT(opts.rxvga2, BLADERF_RXVGA2_GAIN_MIN);
    DEFAULT(opts.file, dup(STDOUT_FILENO));
    DEFAULT(opts.devstr, strdup(""));
    DEFAULT(opts.num_buffers, 32);
    DEFAULT(opts.buffer_size, 8192);
    DEFAULT(opts.num_transfers, 8);
    DEFAULT(opts.timeout_ms, 1000);
    DEFAULT(opts.num_threads, 2);

    // Do we lack a frequency range specification?
    if( argc - optind < 1 ) {
        ERROR("Missing frequency range specification!\n");
        exit(1);
    }

    // Do we have excess arguments?
    if( argc - optind > 1 ) {
        ERROR("Unknown extra arguments, ignoring:\n");
        for( int idx = optind + 1; idx < argc; ++idx )
            ERROR("%s\n", argv[idx]);
    }

    // Parse out the frequency range
    char * freq_range = strdup(argv[optind]);
    unsigned int bin_width;
    if( !parse_freq_range(freq_range, &opts.start_freq, &opts.end_freq, &bin_width) ) {
        free(freq_range);
        ERROR("Unable to parse frequency range specification \"%s\"\n", argv[optind]);
        exit(1);
    }
    free(freq_range);

    // fft_len is the minimum length FFT that guarantees us bins of less than or
    // equal width as requested via bin_width:
    opts.fft_len = (opts.samplerate + bin_width - 1)/bin_width;

    // Calculate how many fft_len buffers we have to stack on top of eachother
    // in order to satisfy the integration time requested by the user.
    if( opts.num_integrations == 0 ) {
        opts.num_integrations = 1;
    } else {
        double fft_ms = opts.fft_len*1000.0/opts.samplerate;
        opts.num_integrations = MAX(ceil(opts.num_integrations/fft_ms), 1);
    }

    // Now that we know our actual fft length, find the true bin width:
    opts.bin_width = opts.samplerate/opts.fft_len;

    // fmbw2 is the amount of spectrum we get with each view, we quantize to our
    // effective bin_width given our bandwidth and number of bins
    #define CALC_FMBW2(fm, bw, len) ceil((fm*bw*len)/2)/len
    opts.fmbw2 = CALC_FMBW2(opts.filter_margin, opts.samplerate, opts.fft_len);

    // Build a frequency plan, store it in opts.freqs and friends
    plan_frequencies(opts.start_freq, opts.end_freq, opts.bin_width, opts.fmbw2);

    // Generate the temporal windowing function
    opts.window = (double *) malloc(sizeof(double)*opts.fft_len);
    gen_window(opts.window_name, opts.window, opts.fft_len);
}

void cleanup_options(void)
{
    close(opts.file);
    free(opts.devstr);
    free(opts.freqs);
    free(opts.window);
    free(opts.window_name);
}
