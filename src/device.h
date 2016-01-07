#include <stdbool.h>
#include <stdint.h>
#include <queue>
#include <time.h>
#include <pthread.h>

struct data_capture {
    int16_t * data;
    unsigned short freq_idx;
    unsigned int integration_idx;
    struct timeval scan_time;
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
int16_t* receive_buffers(unsigned short freq_idx, unsigned int integration_idx,
                         unsigned int *ret_buffs);
bool calibrate_quicktune(void);
