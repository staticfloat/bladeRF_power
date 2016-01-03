#include <stdio.h>
#include <libbladeRF.h>
#include "util.h"
#include "device.h"

void schedule_tuning(unsigned short freq_idx) {
    // Timestamp that next buffer will arrive at, let's retune before then.
    uint64_t next_time = device_data.last_buffer_timestamp + opts.fft_len;

    int status = bladerf_schedule_retune(opts.dev, BLADERF_MODULE_RX, next_time,
                                         opts.freqs[freq_idx], NULL);
    if( status != 0 ) {
        char str[9];
        double2str_suffix(str, opts.freqs[freq_idx], freq_suffixes, NUM_FREQ_SUFFIXES);
        ERROR("bladerf_schedule_retune(dev, rx, %llu, %s, NULL) failed: %s\n",
              next_time, str, bladerf_strerror(status));
    }
}

void receive_buffer() {
    // Our metadata struct to instruct libbladerf on when it should receive data
    struct bladerf_metadata meta;
    memset(&meta, 0, sizeof(meta));
    //meta.flags = BLADERF_META_FLAG_RX_NOW;
    meta.timestamp = device_data.last_buffer_timestamp + opts.fft_len;

    // Wait two times a buffer's length, or a minimum of 5ms.
    unsigned int timeout_ms = 2000*opts.fft_len/opts.samplerate;
    if( timeout_ms < 5 )
        timeout_ms = 5;

    int status = bladerf_sync_rx(opts.dev, device_data.buffer, opts.fft_len, &meta, timeout_ms);
    device_data.last_buffer_timestamp = meta.timestamp;
    if( status != 0 ) {
        ERROR("bladerf_sync_rx(dev, buffer, %d, meta, %d) failed: %s\n", opts.fft_len, timeout_ms, bladerf_strerror(status));
        // Reset timestamp, as it's possible that we have drifted or something
        bladerf_get_timestamp(opts.dev, BLADERF_MODULE_RX, &device_data.last_buffer_timestamp);
        return;
    }
}


void scanner_loop(void) {
    unsigned short freq_idx = 0;
    while( true ) {
        // Schedule a tuning for when we are done receiving data for the current
        // buffer,
        schedule_tuning(freq_idx);
        receive_buffer();

        freq_idx = (freq_idx + 1)%opts.num_freqs;
    }
}


int main(int argc, char ** argv) {
    parse_options(argc, argv);
    bladerf_log_set_verbosity(BLADERF_LOG_LEVEL_DEBUG);
    open_device();

    // Setup quicktuning
    //calibrate_quicktune();
    scanner_loop();

    // Begin scanning loop
    /*
    for( int idx=0; idx<opts.num_freqs; ++idx ) {
        char freq[10];
        double2str_suffix(freq, opts.freqs[idx], freq_suffixes, NUM_FREQ_SUFFIXES);
        printf("freq[%d]: %sHz\n", idx, freq);
    }*/

    close_device();
    return 0;
}
