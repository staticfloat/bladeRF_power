#include <stdbool.h>
#include <stdint.h>
#include <queue>
#include <time.h>
#include <pthread.h>

struct data_capture {
    uint16_t * data;
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
};
extern struct device_data_struct device_data;

bool open_device(void);
void close_device(void);

void schedule_tuning(unsigned short freq_idx);
uint16_t* receive_buffers(unsigned int integration_idx, unsigned int *ret_buffs);

void calibrate_quicktune(void);
