#include <stdbool.h>
#include <stdint.h>
#include <queue>

struct device_data_struct {
    // Buffers of data that require processing
    std::queue<uint16_t *> raw_buffers;

    // Our scratch-space buffer
    uint16_t * buffer;

    // Timestamp next buffer will arrive, used for scheduling retuning/reception
    uint64_t last_buffer_timestamp;
};
extern struct device_data_struct device_data;

bool open_device(void);
void close_device(void);

void calibrate_quicktune(void);
