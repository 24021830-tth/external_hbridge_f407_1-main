#ifndef TEST_STUB_FREERTOS_H
#define TEST_STUB_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))

#endif /* TEST_STUB_FREERTOS_H */
