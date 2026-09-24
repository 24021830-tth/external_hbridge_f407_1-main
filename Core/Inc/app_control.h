/*
 * Application-level control receiver for the DC3W2 protocol.
 *
 * CONTROL payload format, little-endian:
 *  byte 0..1: Left, signed permille (-1000 .. +1000)
 *   byte 2..3: Right, signed permille (-1000 .. +1000)
 *   byte 4:    brake (0 = drive allowed, 1 = stop)
 */
#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "dc3w2_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONTROL_CONTROL_PAYLOAD_SIZE   5U
#define APP_CONTROL_COMMAND_TIMEOUT_MS     200U

typedef struct
{
    int16_t left_permille;
    int16_t right_permille;
    uint8_t brake;
    uint8_t connected;
    uint8_t fresh;
    uint8_t valid;
    uint32_t sequence;
    uint32_t last_update_ms;
    uint32_t age_ms;
} AppControl_Snapshot_t;

/* Call once after USART2 has been initialised. */
void AppControl_Init(void);

/* Process bytes/frames collected by the USART2 interrupt. */
void AppControl_Process(void);

/* USART2 RX interrupt handler; called by USART2_IRQHandler(). */
void AppControl_IrqHandler(void);

/* Copy the latest snapshot. The return value indicates whether a frame exists. */
bool AppControl_GetSnapshot(AppControl_Snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONTROL_H */
