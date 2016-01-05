#include "util.h"
#include <string.h>
#include <unistd.h>
#include <math.h>

// Shamelessly steal the numeric suffixes from bladeRF-cli
const struct numeric_suffix freq_suffixes[NUM_FREQ_SUFFIXES] = {
    { "G",      1000 * 1000 * 1000 },
    { "GHz",    1000 * 1000 * 1000 },
    { "M",      1000 * 1000 },
    { "MHz",    1000 * 1000 },
    { "k",      1000 },
    { "kHz",    1000 }
};

const struct numeric_suffix time_suffixes[NUM_TIME_SUFFIXES] = {
    {"d",  1000*60*60*24 },
    {"h",  1000*60*60 },
    {"m",  1000*60 },
    {"s",  1000},
    {"ms", 1}
};


void time2str(struct timeval &tv, char * out)
{
    int milliseconds = lrint(tv.tv_usec/1000.0);
    if( milliseconds >= 1000 ) {
        milliseconds -= 1000;
        tv.tv_sec++;
    }
    struct tm* tm_info = localtime(&tv.tv_sec);
    size_t len = strftime(out, 26, "%Y:%m:%d %H:%M:%S", tm_info);
    sprintf(out + len, "%03d", milliseconds);
}

const char * fixed_digits(double val, int num_digits)
{
    static char out[10];
    char format[8];
    int num_fractional_digits = MAX(num_digits - log10(val), 0);
    sprintf(format, "%%.%df", num_fractional_digits);
    sprintf(out, format, val);
    return out;
}

int double2str_suffix(char * out, double val, const struct numeric_suffix suffixes[],
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
        return sprintf(out, "%s", fixed_digits(val, 3));
    } else {
        return sprintf(out, "%s%s", fixed_digits(best_ratio, 3), suffixes[best_idx].suffix);
    }
}

unsigned char bladerf_lna_gain_to_db(bladerf_lna_gain lna, bool * ok)
{
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

bladerf_lna_gain bladerf_db_to_lna_gain(unsigned char db, bool * ok)
{
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

void hann(double * rainbow, unsigned int len) {
    for( int idx=0; idx<len; ++idx )
        rainbow[idx] = 0.5 * (1 - cos(2*M_PI*idx/(len - 1)));
}

void hamming(double * window, unsigned int len) {
    for( int idx=0; idx<len; ++idx )
        window[idx] = 0.53836 - 0.46164*cos(2*M_PI*idx/(len - 1));
}

void rect(double * window, unsigned int len) {
    for( int idx=0; idx<len; ++idx)
        window[idx] = 1.0;
}

// If length is zero, we're just checking to see if window_name is valid
bool gen_window(const char * name, double * window, unsigned int len) {
    if( strcasecmp(name, "hann") == 0 ) {
        hann(window, len);
        return true;
    }
    if( strcasecmp(name, "hamming") == 0 ) {
        hamming(window, len);
        return true;
    }
    if( strcasecmp(name, "boxcar") == 0 || strcasecmp(name, "rect") == 0 ||
        strcasecmp(name, "rectangular") == 0) {
        rect(window, len);
        return true;
    }
    return false;
}
