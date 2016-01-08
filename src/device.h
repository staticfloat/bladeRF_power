#include <stdbool.h>
#include <stdint.h>
#include <queue>
#include <time.h>
#include <pthread.h>

struct data_capture {
    // The actual data to be analyzed
    int16_t * data;

    // What frequency and integration index is this data capture?
    unsigned short freq_idx;
    unsigned int integration_idx;

    // What timepoint did this frequency sweep start at?
    struct timeval scan_time;

    // Is this data freeable?  (multiple data_captures may share a single
    // underlying buffer, so only the very first data_capture will be freeable)
    bool freeable;
};

struct device_data_struct {
    // Our bladeRF context object
    struct bladerf *dev;

    // Buffers of data that require processing
    std::queue<struct data_capture> queued_buffers;
    pthread_mutex_t queued_mutex;

    // Timestamp next buffer will arrive, used for scheduling retuning/reception
    uint64_t last_buffer_timestamp;

    // quick tune data calibrated by calibrate_quicktune() and used from within
    // schedule_tuning() to scan through opts.freqs
    struct bladerf_quick_tune * qtunes;
};
extern struct device_data_struct device_data;

bool open_device(void);
void close_device(void);

void schedule_tuning(unsigned short idx, uint64_t timestamp);
bool receive_and_submit_buffers(unsigned short *freq_idx,
                                unsigned int *integration_idx,
                                struct timeval tv_freq);

bool calibrate_quicktune(void);
