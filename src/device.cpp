#include <libbladeRF.h>
#include "device.h"
#include "util.h"

struct device_data_struct device_data;

bool open_device(void) {
    int status;

    LOG("Opening and initializing device...\n");
    status = bladerf_open(&opts.dev, opts.devstr);
    if( status != 0 ) {
        ERROR("Failed to open device: %s\n", bladerf_strerror(status));
        goto out;
    }

    status = bladerf_set_frequency(opts.dev, BLADERF_MODULE_RX, opts.freqs[1]);
    if( status != 0 ) {
        ERROR("Failed to set RX frequency: %s\n", bladerf_strerror(status));
        goto out;
    } else {
        char str[9];
        double2str_suffix(str, opts.freqs[1], freq_suffixes, NUM_FREQ_SUFFIXES);
        INFO("  RX frequency: %sHz\n", str);
    }

    status = bladerf_set_sample_rate(opts.dev, BLADERF_MODULE_RX, opts.samplerate, NULL);
    if( status != 0 ) {
        ERROR("Failed to set RX sample rate: %s\n", bladerf_strerror(status));
        goto out;
    } else {
        char str[9];
        double2str_suffix(str, opts.samplerate, freq_suffixes, NUM_FREQ_SUFFIXES);
        INFO("  RX samplerate: %ssps\n", str);
    }

    status = bladerf_set_bandwidth(opts.dev, BLADERF_MODULE_RX, opts.samplerate, NULL);
    if( status != 0 ) {
        ERROR("Failed to set RX bandwidth: %s\n", bladerf_strerror(status));
        goto out;
    } else {
        char str[9];
        double2str_suffix(str, opts.samplerate, freq_suffixes, NUM_FREQ_SUFFIXES);
        INFO("  RX bandwidth: %sHz\n", str);
    }

    status = bladerf_set_lna_gain(opts.dev, opts.lna);
    if( status != 0 ) {
        ERROR("Failed to set RX LNA gain: %s\n", bladerf_strerror(status));
        goto out;
    } else {
        bool ok;
        int db = bladerf_lna_gain_to_db(opts.lna, &ok);
        INFO("  RX LNA Gain: %ddB\n", db);
    }

    status = bladerf_set_rxvga1(opts.dev, opts.rxvga1);
    if( status != 0 ) {
        ERROR("Failed to set RX VGA1 gain: %s\n", bladerf_strerror(status));
        goto out;
    } else {
        INFO("  RX VGA1 gain: %ddB\n", opts.rxvga1);
    }

    status = bladerf_set_rxvga2(opts.dev, opts.rxvga2);
    if( status != 0 ) {
        ERROR("Failed to set RX VGA2 gain: %s\n", bladerf_strerror(status));
        goto out;
    } else {
        INFO("  RX VGA2 gain: %ddB\n", opts.rxvga2);
    }

    status = bladerf_sync_config(opts.dev, BLADERF_MODULE_RX,
                                 BLADERF_FORMAT_SC16_Q11_META, opts.num_buffers,
                                 opts.buffer_size, opts.num_transfers, opts.timeout_ms);
    if( status != 0 ) {
        ERROR("Failed to sync config: %s\n", bladerf_strerror(status));
        goto out;
    }

    status = bladerf_enable_module(opts.dev, BLADERF_MODULE_RX, true);
    if( status != 0 ) {
        ERROR("Failed to enable RX module: %s\n", bladerf_strerror(status));
        goto out;
    }

    // Get the current timestamp, and then increment it by 10ms
    status = bladerf_get_timestamp(opts.dev, BLADERF_MODULE_RX,
                                   &device_data.last_buffer_timestamp);
    if( status != 0 ) {
        ERROR("Failed to get timestamp: %s\n", bladerf_strerror(status));
        goto out;
    }
    device_data.last_buffer_timestamp += (10*opts.samplerate)/1000;

    // Initialize scratch space buffer
    device_data.buffer = (uint16_t *) malloc(opts.fft_len*2*sizeof(uint16_t));
out:
    if (status != 0) {
        bladerf_close(opts.dev);
        return false;
    }
    return true;
}

void close_device(void) {
    LOG("\nClosing device...");

    /* Disable RX module, shutting down our underlying RX stream */
    int status = bladerf_enable_module(opts.dev, BLADERF_MODULE_RX, false);
    if (status != 0) {
        ERROR("Failed to disable RX module: %s\n", bladerf_strerror(status));
    }

    // Deinitialize and free resources
    bladerf_close(opts.dev);
    LOG(".Done!\n");
}
