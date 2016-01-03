#include "util.h"

// Include libbladeRF conversion code
#include "conversions.h"
#include <libbladeRF.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#define max(x, y) ((x) > (y) ? (x) : (y))


// Instantiate our options struct
struct opts_struct opts;

// Shamelessly steal the numeric suffixes from bladeRF-cli
const struct numeric_suffix freq_suffixes[NUM_FREQ_SUFFIXES] = {
    { "G",      1000 * 1000 * 1000 },
    { "GHz",    1000 * 1000 * 1000 },
    { "M",      1000 * 1000 },
    { "MHz",    1000 * 1000 },
    { "k",      1000 },
    { "kHz",    1000 }
};

void version() {
    printf("bladeRF_power v0.0.1\n");
}

void usage() {
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
    printf("  -h --help                Show this screen.\n");
    printf("  -V --version             Show version.\n");
    printf("  -v --verbose             Raise verbosity level (can be repeated)\n");
    printf("  -z --compress            Compress output with zlib [default: false]\n");
    printf("  -f --file=<outfile>      Write to file `outfile` instead of stdout\n");
    printf("  -e --exit-timer=<et>     Set capture time (example: 5h23m2s) [default: 0]\n");
    printf("  -b --bandwidth=<bw>      Capture bandwidth [default: BLADERF_BANDWIDTH_MAX].\n");
    printf("  -M --filter-margin=<fm>  Anti-aliasing filter margin [default: .85]. This value\n");
    printf("                           is combined with bandwidth to view only a portion of\n");
    printf("                           the captured signal to combat leaky anti-aliasing\n");
    printf("                           filters. Actual useful signal bandwidth is fm*bw/2.\n");
    printf("  -W --window-type=<wt>    Set FFT analysis windowing function [default: hann]\n");
    printf("  -g --lna-gain=<g>        Set LNA gain either as numeric dB value (0, %d, %d) or\n", BLADERF_LNA_GAIN_MID_DB, BLADERF_LNA_GAIN_MAX_DB);
    printf("                           as symbolic (bypass, min, max). [default: max]\n");
    printf("  -o --rx-vga1=<g>         Set vga1 gain either as numeric dB value [%d...%d] or\n", BLADERF_RXVGA1_GAIN_MIN, BLADERF_RXVGA1_GAIN_MAX);
    printf("                           as symbolic (min, max).  [default: min]\n");
    printf("  -o --rx-vga1=<g>         Set vga1 gain either as numeric dB value [%d...%d] or\n", BLADERF_RXVGA2_GAIN_MIN, BLADERF_RXVGA2_GAIN_MAX);
    printf("                           as symbolic (min, max).  [default: min]\n");
    printf("  -d --device=<d>          Device identifier [default: ]\n");
}

static const struct option longopts[] = {
    { "help",               no_argument,        0, 'h' },
    { "version",            no_argument,        0, 'v' },
    { "compress",           no_argument,        0, 'z' },
    { "file",               required_argument,  0, 'f' },
    { "exit-timer",         required_argument,  0, 'e' },
    { "bandwidth",          required_argument,  0, 'b' },
    { "filter-margin",      required_argument,  0, 'M' },
    { "window-type",        required_argument,  0, 'W' },
    { "lna-gain",           required_argument,  0, 'g' },
    { "rxvga1-gain",        required_argument,  0, 'o' },
    { "rxvga2-gain",        required_argument,  0, 'w' },
    { "device",             required_argument,  0, 'd' },
    { 0,                    0,                  0,  0  },
};

const char * fixed_digits(double val, int num_digits) {
    static char out[10];
    char format[5];
    int num_fractional_digits = max(num_digits - log10(val), 0);
    sprintf(format, "%%.%df", num_fractional_digits);
    sprintf(out, format, val);
    return out;
}

void double2str_suffix(char * out, double val, const struct numeric_suffix suffixes[],
                       size_t num_suffixes)
{
    // Search for closest multiplier with shortest suffix
    double best_ratio = val/suffixes[0].multiplier;
    int best_idx = 0;

    // Loop through each suffix
    for( int idx=1; idx<num_suffixes; ++idx ) {
        // First off, always accept a shorter suffix for the same multiplier
        if( suffixes[idx].multiplier == suffixes[best_idx].multiplier ) {
            if( strlen(suffixes[idx].suffix) < strlen(suffixes[best_idx].suffix))
                best_idx = idx;
        } else {
            // Calculate the ratio of this particular index
            double curr_ratio = val/suffixes[idx].multiplier;

            // Only accept ratios greater than one that are still less than best
            if( curr_ratio > 1 && (curr_ratio < best_ratio || best_ratio < 1) ) {
                best_idx = idx;
                best_ratio = curr_ratio;
            }
        }
    }

    // Did we fail to find an acceptable suffix?  If so, print without a suffix.
    if( best_ratio < 1 ) {
        sprintf(out, "%s", fixed_digits(val, 3));
    } else {
        sprintf(out, "%s%s", fixed_digits(best_ratio, 3), suffixes[best_idx].suffix);
    }
}

unsigned char bladerf_lna_gain_to_db(bladerf_lna_gain lna, bool * ok) {
    switch( lna ) {
        default:
            *ok = false;
            return 0;
        case BLADERF_LNA_GAIN_BYPASS:
            *ok = true;
            return 0;
        case BLADERF_LNA_GAIN_MID:
            *ok = true;
            return BLADERF_LNA_GAIN_MID_DB;
        case BLADERF_LNA_GAIN_MAX:
            *ok = true;
            return BLADERF_LNA_GAIN_MAX_DB;
    }
}

bladerf_lna_gain bladerf_db_to_lna_gain(unsigned char db, bool * ok) {
    switch( db ) {
        case BLADERF_LNA_GAIN_MAX_DB:
            *ok = true;
            return BLADERF_LNA_GAIN_MAX;
        case BLADERF_LNA_GAIN_MID_DB:
            *ok = true;
            return BLADERF_LNA_GAIN_MID;
        case 0:
            *ok = true;
            return BLADERF_LNA_GAIN_BYPASS;
        default:
            *ok = false;
            return BLADERF_LNA_GAIN_UNKNOWN;
    }
}


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
                      unsigned int bin_width, unsigned int fmbw2)
{
    // How many frequencies will we need to tune to? (add fbmw2 - 1 on to the
    // total bandwidth to round up).
    opts.num_freqs = (end_freq - start_freq + fmbw2 - 1)/fmbw2;
    opts.freqs = (unsigned int *) malloc(sizeof(unsigned int)*opts.num_freqs);

    if( start_freq - bin_width >= BLADERF_FREQUENCY_MIN ) {
        // Put freqs[0] just below start_freq if we are not already at the
        // minimum frequency, and thus unable to tune below start_freq
        opts.freqs[0] = start_freq - bin_width;
        opts.first_freq_lower_sideband = false;
    } else {
        // Otherwise, put center_freq just above start_freq + bandwidth/2 and
        // capture the lower sideband of this view
        opts.freqs[0] = start_freq + fmbw2;
        opts.first_freq_lower_sideband = true;
    }

    // Fill in the rest of the center frequencies
    for( int idx=1; idx < opts.num_freqs; ++idx )
        opts.freqs[idx] = start_freq - bin_width + idx*fmbw2;
}



// Macro to set default values that are initialized to zero
#define DEFAULT(field, val) if( field == 0 ) { field = val; }
#define OPTSTR "hvzf:e:b:M:W:g:o:w:d:"

void parse_options(int argc, char ** argv)
{
    // First thing we do is initialize the entire opts struct to zero
    memset(&opts, sizeof(opts), 0);

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
                opts.file = open(optarg, O_CREAT | O_TRUNC);
                if( opts.file == -1 ) {
                    ERROR("Unable to open \"%s\" for writing!\n", optarg);
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
                if( ! ok ) {
                    ERROR("Invalid RXVGA1 gain \"%s\"\n", optarg);
                    ERROR("Valid range: [%u, %u]\n", BLADERF_RXVGA1_GAIN_MIN, BLADERF_RXVGA1_GAIN_MAX);
                    exit(1);
                }
                break;
            case 'w':
                opts.rxvga2 = str2uint(optarg, BLADERF_RXVGA2_GAIN_MIN,
                                    BLADERF_RXVGA2_GAIN_MAX, &ok);
                if( ! ok ) {
                    ERROR("Invalid RXVGA2 gain \"%s\"\n", optarg);
                    ERROR("Valid range: [%u, %u]\n", BLADERF_RXVGA2_GAIN_MIN, BLADERF_RXVGA2_GAIN_MAX);
                    exit(1);
                }
                break;
            case 'd':
                opts.devstr = strdup(optarg);
                break;
        }

        c = getopt_long(argc, argv, OPTSTR, longopts, &optidx);
    } while (c != -1);

    // Set defaults for everything that didn't get an explicit value:
    DEFAULT(opts.verbosity, 0);
    DEFAULT(opts.samplerate, BLADERF_BANDWIDTH_MAX);
    DEFAULT(opts.filter_margin, 0.85);
    DEFAULT(opts.lna, BLADERF_LNA_GAIN_MAX);
    DEFAULT(opts.rxvga1, BLADERF_RXVGA1_GAIN_MIN);
    DEFAULT(opts.rxvga2, BLADERF_RXVGA2_GAIN_MIN);
    DEFAULT(opts.file, dup(STDOUT_FILENO));
    DEFAULT(opts.devstr, "");
    DEFAULT(opts.num_buffers, 32);
    DEFAULT(opts.buffer_size, 8192);
    DEFAULT(opts.num_transfers, 8);
    DEFAULT(opts.timeout_ms, 1000);

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
    unsigned int start_freq, end_freq, bin_width;
    if( !parse_freq_range(freq_range, &start_freq, &end_freq, &bin_width) ) {
        free(freq_range);
        ERROR("Unable to parse frequency range specification \"%s\"\n", argv[optind]);
        exit(1);
    }
    free(freq_range);

    // fft_len is the minimum length FFT that guarantees us bins of less than or
    // equal width as requested via bin_width:
    opts.fft_len = (opts.samplerate + bin_width - 1)/bin_width;

    // Now that we know our actual fft length, find the true bin width:
    bin_width = opts.samplerate/opts.fft_len;

    // fmbw2 is the amount of spectrum we get with each view, we quantize to our
    // effective bin_width given our bandwidth and number of bins
    unsigned int fmbw2 = (opts.filter_margin*(opts.samplerate/2)*opts.fft_len + opts.fft_len/2)/opts.fft_len;

    // Build a frequency plan, store it in opts.freqs and friends
    plan_frequencies(start_freq, end_freq, bin_width, fmbw2);
}
