#include <stdio.h>
#include <libbladeRF.h>
#include "util.h"
#include "device.h"
#include "options.h"
#include "worker.h"
#include <math.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>


bool keep_running = true;
struct sigaction old_sigint_action;

void sigint_handler(int dummy)
{
    LOG("\nGracefully shutting down...");
    keep_running = false;
    sigaction(SIGINT, &old_sigint_action, NULL);
}

#define STATUS_LINE_LEN     100
#define STATUS_LINE_BINS    64
void print_status_line(unsigned short freq_idx, unsigned int ms_elapsed)
{
    char status_line[STATUS_LINE_LEN+1];
    status_line[0] = '[';
    status_line[STATUS_LINE_BINS+1] = ']';
    status_line[STATUS_LINE_BINS+2] = ' ';

    // Quantize spectrum into STATUS_LINE_BINS bins
    float bin_width = (opts.end_freq - opts.start_freq)/STATUS_LINE_BINS;
    int center_idx = lrint((opts.freqs[freq_idx] - opts.start_freq)/bin_width);
    if( !opts.first_freq_lower_sideband && freq_idx == 0 )
        center_idx = 0;
    int bandwidth = (int)opts.fmbw2/bin_width;

    for( int idx = 1; idx <= STATUS_LINE_BINS; idx++ ) {
        // Are we doing a lower sideband right now?
        if( freq_idx == 0 && opts.first_freq_lower_sideband ) {
            if( idx >= center_idx - bandwidth && idx < center_idx )
                status_line[idx] = '.';
            else
                status_line[idx] = ' ';
        } else {
            if( idx <= center_idx + bandwidth && idx > center_idx )
                status_line[idx] = '.';
            else
                status_line[idx] = ' ';
        }
    }

    // Write out center_frequency spike
    status_line[center_idx] = '|';

    // Write out actual center frequency at the end:
    int offset = double2str_suffix(status_line + STATUS_LINE_BINS + 3,
                                   opts.freqs[freq_idx], freq_suffixes,
                                   NUM_FREQ_SUFFIXES);
    offset += STATUS_LINE_BINS + 3;
    status_line[offset++] = 'H';
    status_line[offset++] = 'z';

    // Print out how many queued buffers are waiting to be processed
    offset += snprintf(status_line + offset, STATUS_LINE_LEN - offset,
                       " Q: %4lu", device_data.queued_buffers.size());

    // Print out how many seconds/minutes/hours have passed
    if( opts.exit_timer > 0 ) {
        float pct = MIN(ms_elapsed*100.0/opts.exit_timer, 100.0f);
        offset += snprintf(status_line + offset, STATUS_LINE_LEN - offset,
                           "  T: %.1fs/%5.1f%%", ms_elapsed/1000.0, pct);
    }
    else {
        offset += snprintf(status_line + offset, STATUS_LINE_LEN - offset,
                           "  T: %.1fs/\u221E", ms_elapsed/1000.0);
    }

    while( offset < STATUS_LINE_LEN-1 )
        status_line[offset++] = ' ';
    status_line[STATUS_LINE_LEN-1] = '\r';
    status_line[STATUS_LINE_LEN] = '\0';
    fputs(status_line, stderr);
}

int main(int argc, char ** argv)
{
    parse_options(argc, argv);

    if( opts.verbosity > 2 )
        bladerf_log_set_verbosity(BLADERF_LOG_LEVEL_DEBUG);

    // Open our bladeRF
    if( !open_device() )
        return 1;

    // Setup quicktuning information so as to make tuning quick and easy
    if( !calibrate_quicktune() )
        return 1;

    // Start worker threads
    start_worker_threads();

    // Setup SIGINT handler so we can gracefully quit
    struct sigaction act;
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, &old_sigint_action);

    // Begin scanning loop
    timeval tv_start, tv_freq, tv_status, tv_tune, tv;
    gettimeofday(&tv_start, NULL);
    tv_status = tv_start;
    tv_tune = tv_start;

    unsigned short freq_idx = 0;
    unsigned int integration_idx = 0;
    unsigned int failures_in_a_row = 0;
    while( keep_running ) {
        // Always get current time
        gettimeofday(&tv, NULL);

        // Save the time of the start of this run through frequency
        if( freq_idx == 0 && integration_idx == 0 ) {
            tv_freq = tv;

            // Check to see if we've overstayed our welcome
            if( opts.exit_timer != 0 && msdiff(tv, tv_start) > opts.exit_timer ) {
                keep_running = false;
                break;
            }
        }

        // Update status_line a maximum of 20 times a second
        if( msdiff(tv, tv_status) >= 50 ) {
            print_status_line(freq_idx, msdiff(tv, tv_start));
            tv_status = tv;
        }

        // Receives buffers of data, submits them to FFT workers, moves freq_idx
        // and integration_idx forward if need be, and returns false if fails
        unsigned int num_buffs;
        if( receive_and_submit_buffers(&freq_idx, &integration_idx, tv_freq) ) {
            failures_in_a_row = 0;
        } else {
            failures_in_a_row++;
            if( failures_in_a_row > 5 ) {
                // If we've failed five times in a row, close the device and re-open
                LOG("Failure threshold reached, closing and reopening device...\n");
                close_device();
                open_device();
                calibrate_quicktune();
                failures_in_a_row = 0;
            }
        }

        // Recalibrate quicktune once per hour
        if( msdiff(tv, tv_tune) > 60*60*1000 ) {
            LOG("Recalibrating quicktune parameters...\n");
            calibrate_quicktune();
            tv_tune = tv;
        }
    }

    // Stop worker threads
    stop_worker_threads();
    close_device();
    cleanup_options();
    LOG("Shutdown complete!\n")
    return 0;
}
