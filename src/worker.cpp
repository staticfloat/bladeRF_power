#include "worker.h"
#include "options.h"
#include "device.h"
#include "util.h"
#include <pthread.h>
#include <string.h>
#include <fftw3.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

static pthread_t * threads;
static pthread_mutex_t fftw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t integration_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool keep_running;

struct integration_buffer {
    double * data;
    unsigned int num_integrations;
    unsigned int freq_idx;
};

// When integrating into a buffer, store data from a particular frequency into
// an "integration_buffer" stored within this vector of "integration_buffers";
// once the buffer has racked up a number of integrations equal to what was
// requested by the used via opts.num_integrations, we flush it out to disk and
// clear the structure from this vector.
std::vector<integration_buffer *> integration_buffers;


double db(double x)
{
    return 20*log10(x);
}

double abs(fftw_complex x)
{
    return x[0]*x[0] + x[1]*x[1];
}

bool get_next_buffer(struct data_capture * buffer)
{
    pthread_mutex_lock(&fftw_mutex);
    pthread_mutex_lock(&device_data.queued_mutex);
    if( !device_data.queued_buffers.empty() ) {
        *buffer = device_data.queued_buffers.front();
        device_data.queued_buffers.pop();
        pthread_mutex_unlock(&fftw_mutex);
        pthread_mutex_unlock(&device_data.queued_mutex);
        return true;
    }
    pthread_mutex_unlock(&fftw_mutex);
    pthread_mutex_unlock(&device_data.queued_mutex);
    return false;
}

struct integration_buffer * get_integration_buffer(unsigned short freq_idx,
                                                   unsigned int num_bins)
{
    int ib_idx = -1;
    for( int idx = 0; idx < integration_buffers.size(); ++idx ) {
        if( integration_buffers[idx]->freq_idx == freq_idx ) {
            ib_idx = idx;
            break;
        }
    }

    if( ib_idx == -1 ) {
        // Let's create a new integration_buffer!
        struct integration_buffer * ib = (struct integration_buffer *) malloc(sizeof(struct integration_buffer));
        ib->data = (double *) malloc(sizeof(double)*num_bins);
        memset(ib->data, 0, sizeof(double)*num_bins);
        ib->num_integrations = 0;
        ib->freq_idx = freq_idx;

        // Add it into the vector:
        integration_buffers.push_back(ib);
        return ib;
    }
    return integration_buffers[ib_idx];
}

void * worker(void * arg)
{
    // Create FFT buffers, plans and string holders
    fftw_complex *data;
    fftw_plan plan;

    // Roughly the length of the maximum csv line we will construct
    char * csv_line = (char *) malloc(64 + 10*opts.fft_len);

    // We lock these portions because we know FFTW is mostly thread-unsafe
    pthread_mutex_lock(&fftw_mutex);
    data = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * opts.fft_len);
    plan = fftw_plan_dft_1d(opts.fft_len, data, data, FFTW_FORWARD, FFTW_ESTIMATE);
    pthread_mutex_unlock(&fftw_mutex);

    while( keep_running ) {
        struct data_capture buffer;
        if( get_next_buffer(&buffer) ) {
            // Transform data into fftw_complex
            for( int idx=0; idx<opts.fft_len; ++idx ) {
                data[idx][0] = buffer.data[2*idx+0]/2048.0 * opts.window[idx];
                data[idx][1] = buffer.data[2*idx+1]/2048.0 * opts.window[idx];
            }

            // Take FFT of data, "data" now holds the DFT of buffer.data
            fftw_execute(plan);

            // We're going to need to find the following pieces of Information
            // to write out a complete csv line.  First off, the start and end
            // of the current "view" of frequency:
            unsigned int view_start, view_end;

            // We're also going to need to know which bins to capture from data:
            unsigned int bin_start, bin_end;

            // Figure out which sideband we are looking at, this informs the
            // values of view_start/view_end, and bin_start/bin_end
            unsigned int center_freq = opts.freqs[buffer.freq_idx];

            if( buffer.freq_idx == 0 && opts.first_freq_lower_sideband ) {
                view_start = MAX(center_freq - opts.fmbw2, opts.start_freq);
                view_end = MIN(center_freq - opts.bin_width, opts.end_freq);

                // Lower sideband requires a + fft_len to start grabbing from
                // the second half of the FFT output buffer
                bin_start = opts.fft_len - (center_freq - view_start)/opts.bin_width;
                bin_end = opts.fft_len - (center_freq - view_end)/opts.bin_width + 1;
            } else {
                view_start = MAX(center_freq + opts.bin_width, opts.start_freq);
                view_end = MIN(center_freq + opts.fmbw2, opts.end_freq);

                bin_start = (view_start - center_freq)/opts.bin_width;
                bin_end = (view_end - center_freq)/opts.bin_width + 1;
            }

            // Convert the interesting bits of the spectrum to absolute values,
            // store the absolute values into the real part of "data".
            for( int idx = bin_start; idx < bin_end; ++idx )
                data[idx][0] = abs(data[idx]);

            // Add this buffer to integration_buffers
            unsigned int num_bins = bin_end - bin_start;
            pthread_mutex_lock(&integration_mutex);
            struct integration_buffer * ib = get_integration_buffer(buffer.freq_idx, num_bins);

            // Accumulate the interesting bits of the spectrum into ib->data
            for( int idx = 0; idx < num_bins; ++idx )
                ib->data[idx] += data[bin_start + idx][0];

            // Increment the number of integrations, and cleanup if we are ready!
            ib->num_integrations++;
            if( ib->num_integrations >= opts.num_integrations ) {
                // Since we're cleaning up, we know that nobody else is messing
                // with this integration buffer, so we release the lock here
                pthread_mutex_unlock(&integration_mutex);

                //printf("FLUSHING BUFFER FOR %d\n", buffer.freq_idx);
                // Start constructing csv_line
                int len = sprintf(csv_line, "%lu, '', %u, %u, %.3f, %d, ",
                                  buffer.scan_time.tv_sec, view_start,
                                  view_end, opts.bin_width, opts.fft_len);

                // Convert accumulated data to dB and shove into csv_line
                for( int idx=0; idx<num_bins - 1; ++idx ) {
                    len += sprintf(csv_line + len, "%.3f, ",
                                   db(ib->data[idx]/opts.num_integrations));
                }

                // Write out last line
                len += sprintf(csv_line + len, "%.3f\n",
                               db(ib->data[num_bins-1]/opts.num_integrations));

                // Write out to file
                write(opts.file, csv_line, len);

                // Cleanup integration_buffer, re-obtaining the mutex for this.
                pthread_mutex_lock(&integration_mutex);
                #define DEL(v, x) v.erase(std::remove(v.begin(), v.end(), x), v.end())
                DEL(integration_buffers, ib);
                pthread_mutex_unlock(&integration_mutex);

                free(ib->data);
                free(ib);
            } else
                pthread_mutex_unlock(&integration_mutex);

        } else {
            // Sleep a little if we didn't have anything to process, waiting for
            // the next bit of data, so that we're not casually burning the CPU
            usleep(1);
        }
    }

    fftw_free(data);
    fftw_destroy_plan(plan);
    return NULL;
}

void start_worker_threads()
{
    keep_running = true;
    int status;

    // For each thread,
    threads = (pthread_t *) malloc(sizeof(pthread_t)*opts.num_threads);
    for( int idx=0; idx<opts.num_threads; ++idx ) {
        status = pthread_create(&threads[idx], NULL, &worker, NULL);
        if( status != 0 ) {
            ERROR("Could not start thread[%d]: %s\n", idx, strerror(status));
            exit(1);
        }
    }
}

void stop_worker_threads()
{
    // Wait up to 10 seconds for FFT buffers to dry up
    while( !device_data.queued_buffers.empty() ) {
        LOG("Waiting for %3d queued buffers to process...  \r", device_data.queued_buffers.size());
        usleep(1*1000);
    }
    keep_running = false;
    for( int idx=0; idx<opts.num_threads; ++idx ) {
        pthread_join(threads[idx], NULL);
    }
    free(threads);
    fftw_cleanup();
}
