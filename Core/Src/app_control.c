#include "app_control.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "main.h"
#include "stm32f4xx_ll_usart.h"
#include "task.h"
#include "usart6_log.h"

/*
 * USART2 is received in the interrupt, like the known-good reference main.c.
 * The RTOS task only consumes the queues and performs the slower application
 * work (logging, ACK and motor snapshot update).
 */
#define APP_CONTROL_RX_BUDGET_PER_PROCESS  256U
#define APP_CONTROL_RX_QUEUE_SIZE          512U
#define APP_CONTROL_FRAME_QUEUE_SIZE       4U
#define APP_CONTROL_TX_WAIT_LOOPS          1000000U
#define APP_CONTROL_LOG_PERIOD_MS          0U
#define APP_CONTROL_DECODE_LOG_PERIOD_MS   0U

/*
 * Select the two independent diagnostic paths here:
 *   VALID_FRAME_PATH = 1: decode DC3W2 and allow control/motor updates.
 *   RAW_BYTE_PATH    = 1: log every byte received from USART2 as HEX.
 *
 * Both paths are enabled for the current diagnostic:
 * invalid/incomplete data is still visible as raw HEX, while valid data is
 * decoded and forwarded to the vehicle controller.
 */
#define APP_CONTROL_ENABLE_VALID_FRAME_PATH  1U
#define APP_CONTROL_ENABLE_RAW_BYTE_PATH     0U // Data raw
#define APP_CONTROL_ENABLE_DECODE_LOG       0U
#define APP_CONTROL_RAW_CAPTURE_SIZE         64U
#define APP_CONTROL_RAW_LOG_PERIOD_MS        100U

/*
 * The current app sends CONTROL directly.  PROBE is still decoded and ACKed,
 * but it is not required before a valid CONTROL frame is accepted.
 */
#define APP_CONTROL_REQUIRE_PI_PROBE         0U
#define APP_CONTROL_ROLE_NONE                0U
#define APP_CONTROL_ROLE_PI                  1U
#define APP_CONTROL_ROLE_LAPTOP              2U

/* ACK status values used by the old reference application. */
#define APP_CONTROL_ACK_OK                   0U
#define APP_CONTROL_ACK_BAD_FRAME            1U
#define APP_CONTROL_ACK_BAD_DATA             2U

static volatile AppControl_Snapshot_t s_snapshot;
static volatile uint32_t s_rx_error_count;
static volatile uint32_t s_rx_overrun_count;
static uint32_t s_reported_rx_error_count;
static uint32_t s_reported_rx_overrun_count;

#if (APP_CONTROL_ENABLE_RAW_BYTE_PATH != 0U)
static volatile uint8_t s_rx_queue[APP_CONTROL_RX_QUEUE_SIZE];
static volatile uint16_t s_rx_queue_head;
static volatile uint16_t s_rx_queue_tail;
static uint8_t s_raw_capture[APP_CONTROL_RAW_CAPTURE_SIZE];
static uint16_t s_raw_capture_length;
static uint32_t s_last_raw_log_ms;
#endif

#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
static Dc3w2Decoder s_decoder;
static volatile Dc3w2Frame s_frame_queue[APP_CONTROL_FRAME_QUEUE_SIZE];
static volatile uint8_t s_frame_queue_head;
static volatile uint8_t s_frame_queue_tail;
static uint8_t s_role;
static uint32_t s_last_sequence;
static uint8_t s_has_sequence;
static uint32_t s_last_log_ms;
static uint8_t s_has_logged_control;
static uint32_t s_last_decoded_log_ms;
static uint8_t s_has_logged_decoded;
#endif

static uint32_t AppControl_NowMs(void)
{
    /* configTICK_RATE_HZ is 1000 in this project, so one tick is one ms. */
    return (uint32_t)xTaskGetTickCount();
}

#if (APP_CONTROL_ENABLE_RAW_BYTE_PATH != 0U)
static uint16_t AppControl_NextRxIndex(uint16_t index)
{
    ++index;
    return (index >= APP_CONTROL_RX_QUEUE_SIZE) ? 0U : index;
}

static void AppControl_QueueRxByte(uint8_t byte)
{
    uint16_t next_head = AppControl_NextRxIndex(s_rx_queue_head);

    if (next_head == s_rx_queue_tail)
    {
        ++s_rx_overrun_count;
#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
        /* A dropped byte invalidates the current frame candidate. */
        Dc3w2_DecoderInit(&s_decoder);
#endif
        return;
    }

    s_rx_queue[s_rx_queue_head] = byte;
    s_rx_queue_head = next_head;
}

static uint8_t AppControl_DequeueRxByte(uint8_t *byte)
{
    uint16_t tail;

    if ((byte == NULL) || (s_rx_queue_tail == s_rx_queue_head))
    {
        return 0U;
    }

    tail = s_rx_queue_tail;
    *byte = s_rx_queue[tail];
    s_rx_queue_tail = AppControl_NextRxIndex(tail);
    return 1U;
}
#endif

#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
static void AppControl_QueueFrame(const Dc3w2Frame *frame)
{
    uint8_t next_head;

    next_head = (uint8_t)((s_frame_queue_head + 1U) %
                          APP_CONTROL_FRAME_QUEUE_SIZE);
    if (next_head == s_frame_queue_tail)
    {
        ++s_rx_overrun_count;
        Dc3w2_DecoderInit(&s_decoder);
        return;
    }

    s_frame_queue[s_frame_queue_head] = *frame;
    s_frame_queue_head = next_head;
}

static uint8_t AppControl_DequeueFrame(Dc3w2Frame *frame)
{
    uint8_t tail;

    if ((frame == NULL) || (s_frame_queue_tail == s_frame_queue_head))
    {
        return 0U;
    }

    tail = s_frame_queue_tail;
    *frame = s_frame_queue[tail];
    s_frame_queue_tail = (uint8_t)((tail + 1U) %
                                   APP_CONTROL_FRAME_QUEUE_SIZE);
    return 1U;
}

static int16_t AppControl_ReadI16LE(const uint8_t *data)
{
    uint16_t value;

    value = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
    return (int16_t)value;
}

static uint8_t AppControl_IsPermilleValid(int16_t value)
{
    return ((value >= -1000) && (value <= 1000)) ? 1U : 0U;
}

static uint8_t AppControl_IsNewSequence(uint32_t sequence)
{
    if (s_has_sequence == 0U)
    {
        return 1U;
    }

    /* Signed comparison handles normal 32-bit sequence wrap-around. */
    return ((int32_t)(sequence - s_last_sequence) > 0) ? 1U : 0U;
}

static uint8_t AppControl_SendFrame(uint8_t frame_type,
                                    uint32_t sequence,
                                    const uint8_t *payload,
                                    uint16_t payload_length)
{
    uint8_t output[DC3W2_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t index;

    length = Dc3w2_Encode(frame_type, 0U, sequence,
                          payload, payload_length,
                          output, sizeof(output));
    if (length == 0U)
    {
        return 0U;
    }

    for (index = 0U; index < length; ++index)
    {
        uint32_t wait_count = 0U;

        while ((LL_USART_IsActiveFlag_TXE(USART2) == 0U) &&
               (wait_count < APP_CONTROL_TX_WAIT_LOOPS))
        {
            ++wait_count;
        }

        if (LL_USART_IsActiveFlag_TXE(USART2) == 0U)
        {
            return 0U;
        }

        LL_USART_TransmitData8(USART2, output[index]);
    }

    /* Do not let the response remain half-transmitted when the task returns. */
    {
        uint32_t wait_count = 0U;

        while ((LL_USART_IsActiveFlag_TC(USART2) == 0U) &&
               (wait_count < APP_CONTROL_TX_WAIT_LOOPS))
        {
            ++wait_count;
        }
    }

    return 1U;
}

static void AppControl_SendAck(uint32_t sequence,
                               uint8_t acknowledged_type,
                               uint8_t status)
{
    uint8_t payload[3];

    /* ACK payload: acknowledged type, status, current port ID (USART2 = 2). */
    payload[0] = acknowledged_type;
    payload[1] = status;
    payload[2] = 2U;
    (void)AppControl_SendFrame(DC3W2_TYPE_ACK, sequence,
                               payload, sizeof(payload));
}

static void AppControl_UpdateHealth(uint32_t now)
{
    taskENTER_CRITICAL();

    if (s_snapshot.valid != 0U)
    {
        s_snapshot.age_ms = (uint32_t)(now - s_snapshot.last_update_ms);
        if (s_snapshot.age_ms <= APP_CONTROL_COMMAND_TIMEOUT_MS)
        {
            s_snapshot.connected = 1U;
            s_snapshot.fresh = 1U;
        }
        else
        {
            s_snapshot.connected = 0U;
            s_snapshot.fresh = 0U;
        }
    }
    else
    {
        s_snapshot.connected = 0U;
        s_snapshot.fresh = 0U;
        s_snapshot.age_ms = 0U;
    }

    taskEXIT_CRITICAL();
}

static void AppControl_HandleFrame(const Dc3w2Frame *frame)
{
    AppControl_Snapshot_t next_snapshot;
    uint32_t now;
    int16_t left;
    int16_t right;

    if (frame == NULL)
    {
        return;
    }

    /* Restored from the old main.c: identify the sender before CONTROL. */
    if ((frame->frame_type == DC3W2_TYPE_PROBE) &&
        (frame->payload_length == 1U) &&
        ((frame->payload[0] == APP_CONTROL_ROLE_PI) ||
         (frame->payload[0] == APP_CONTROL_ROLE_LAPTOP)))
    {
        s_role = frame->payload[0];
        AppControl_SendAck(frame->seq, frame->frame_type,
                           APP_CONTROL_ACK_OK);
       /* Usart6Log_Write((s_role == APP_CONTROL_ROLE_PI) ?
                        "[USART2] PROBE role=PI ACK=OK\r\n" :
                        "[USART2] PROBE role=LAPTOP ACK=OK\r\n");
        */
        return;
    }

    if ((frame->frame_type != DC3W2_TYPE_CONTROL) ||
        (frame->payload_length != APP_CONTROL_CONTROL_PAYLOAD_SIZE) ||
        ((APP_CONTROL_REQUIRE_PI_PROBE != 0U) &&
         (s_role != APP_CONTROL_ROLE_PI)))
    {
        AppControl_SendAck(frame->seq, frame->frame_type,
                           APP_CONTROL_ACK_BAD_FRAME);
        // Usart6Log_Write("[USART2] CONTROL rejected: need PI PROBE/type/length\r\n");
        return;
    }

    left = AppControl_ReadI16LE(&frame->payload[0]);
    right = AppControl_ReadI16LE(&frame->payload[2]);
    if ((AppControl_IsPermilleValid(left) == 0U) ||
        (AppControl_IsPermilleValid(right) == 0U) ||
        (frame->payload[4] > 1U) ||
        (AppControl_IsNewSequence(frame->seq) == 0U))
    {
        AppControl_SendAck(frame->seq, frame->frame_type,
                           APP_CONTROL_ACK_BAD_DATA);
        // Usart6Log_Write("[USART2] CONTROL rejected: data/sequence\r\n");
        return;
    }

    memset(&next_snapshot, 0, sizeof(next_snapshot));
    next_snapshot.left_permille = left;
    next_snapshot.right_permille = right;
    next_snapshot.brake = frame->payload[4];
    next_snapshot.sequence = frame->seq;
    next_snapshot.valid = 1U;

    now = AppControl_NowMs();
    next_snapshot.last_update_ms = now;
    next_snapshot.age_ms = 0U;
    next_snapshot.connected = 1U;
    next_snapshot.fresh = 1U;

    s_last_sequence = frame->seq;
    s_has_sequence = 1U;

    taskENTER_CRITICAL();
    s_snapshot = next_snapshot;
    taskEXIT_CRITICAL();

    AppControl_SendAck(frame->seq, frame->frame_type,
                       APP_CONTROL_ACK_OK);

    /*if ((s_has_logged_control == 0U) ||
        ((uint32_t)(now - s_last_log_ms) >= APP_CONTROL_LOG_PERIOD_MS))
    {
        Usart6Log_Control(next_snapshot.sequence,
                          next_snapshot.left_permille,
                          next_snapshot.right_permille,
                          next_snapshot.brake,
                          frame->flags);
        s_last_log_ms = now;
        s_has_logged_control = 1U;
    }
        */
}
#endif

void AppControl_IrqHandler(void)
{
    uint32_t status;

    /* Read SR before DR, as required to clear STM32F4 USART error flags. */
    status = USART2->SR;
    if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U)
    {
        (void)USART2->DR;
        ++s_rx_error_count;
#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
        Dc3w2_DecoderInit(&s_decoder);
#endif
    }

    /* If the byte was associated with an error, it was deliberately dropped. */
    if ((USART2->SR & USART_SR_RXNE) != 0U)
    {
        uint8_t byte = (uint8_t)(USART2->DR & 0xFFU);

#if (APP_CONTROL_ENABLE_RAW_BYTE_PATH != 0U)
        AppControl_QueueRxByte(byte);
#endif

#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
        {
            Dc3w2Frame frame;

            /* DecoderFeed is pure byte/state processing and runs per RX byte. */
            if (Dc3w2_DecoderFeed(&s_decoder, byte, &frame) != 0U)
            {
                AppControl_QueueFrame(&frame);
            }
        }
#endif
    }
}

void AppControl_Init(void)
{
    /* Do not leave an old byte or a pending IRQ in the receiver. */
    LL_USART_DisableIT_RXNE(USART2);
    NVIC_DisableIRQ(USART2_IRQn);

    {
        volatile uint32_t status = USART2->SR;
        volatile uint32_t data = USART2->DR;

        (void)status;
        (void)data;
    }

    NVIC_ClearPendingIRQ(USART2_IRQn);

    taskENTER_CRITICAL();
    memset((void *)&s_snapshot, 0, sizeof(s_snapshot));
    taskEXIT_CRITICAL();

    s_rx_error_count = 0U;
    s_rx_overrun_count = 0U;
    s_reported_rx_error_count = 0U;
    s_reported_rx_overrun_count = 0U;

#if (APP_CONTROL_ENABLE_RAW_BYTE_PATH != 0U)
    s_rx_queue_head = 0U;
    s_rx_queue_tail = 0U;
    s_raw_capture_length = 0U;
    s_last_raw_log_ms = 0U;
#endif

#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
    Dc3w2_DecoderInit(&s_decoder);
    s_frame_queue_head = 0U;
    s_frame_queue_tail = 0U;
    s_role = APP_CONTROL_ROLE_NONE;
    s_last_sequence = 0U;
    s_has_sequence = 0U;
    s_last_log_ms = 0U;
    s_has_logged_control = 0U;
    s_last_decoded_log_ms = 0U;
    s_has_logged_decoded = 0U;
#endif

    NVIC_SetPriority(USART2_IRQn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                          5U, 0U));
    NVIC_EnableIRQ(USART2_IRQn);
    LL_USART_EnableIT_RXNE(USART2);
}

void AppControl_Process(void)
{
    // uint32_t now = AppControl_NowMs();

#if (APP_CONTROL_ENABLE_RAW_BYTE_PATH != 0U)
    {
        uint32_t count = 0U;
        uint8_t byte;

        while ((count < APP_CONTROL_RX_BUDGET_PER_PROCESS) &&
               (AppControl_DequeueRxByte(&byte) != 0U))
        {
            if (s_raw_capture_length < APP_CONTROL_RAW_CAPTURE_SIZE)
            {
                s_raw_capture[s_raw_capture_length++] = byte;
            }

            if (s_raw_capture_length == APP_CONTROL_RAW_CAPTURE_SIZE)
            {
                Usart6Log_Raw(s_raw_capture, s_raw_capture_length);
                s_raw_capture_length = 0U;
                s_last_raw_log_ms = now;
            }
            ++count;
        }

        if ((s_raw_capture_length > 0U) &&
            ((uint32_t)(now - s_last_raw_log_ms) >=
             APP_CONTROL_RAW_LOG_PERIOD_MS))
        {
            Usart6Log_Raw(s_raw_capture, s_raw_capture_length);
            s_raw_capture_length = 0U;
            s_last_raw_log_ms = now;
        }
    }
#endif

#if (APP_CONTROL_ENABLE_VALID_FRAME_PATH != 0U)
    {
        Dc3w2Frame frame;

        while (AppControl_DequeueFrame(&frame) != 0U)
        {
            /* This is the same point as binary_ready in the old main.c. */
#if (APP_CONTROL_ENABLE_DECODE_LOG != 0U)
            if ((s_has_logged_decoded == 0U) ||
                ((uint32_t)(now - s_last_decoded_log_ms) >=
                 APP_CONTROL_DECODE_LOG_PERIOD_MS))
            {
                //=========================================
                Usart6Log_DecodedFrame(&frame);
                s_last_decoded_log_ms = now;
                s_has_logged_decoded = 1U;
            }
#endif
            AppControl_HandleFrame(&frame);
        }

        AppControl_UpdateHealth(AppControl_NowMs());
    }
#endif

    if (s_rx_error_count != s_reported_rx_error_count)
    {
        s_reported_rx_error_count = s_rx_error_count;
        // Usart6Log_Write("[USART2] UART error: decoder reset\r\n");
    }

    if (s_rx_overrun_count != s_reported_rx_overrun_count)
    {
        s_reported_rx_overrun_count = s_rx_overrun_count;
        // Usart6Log_Write("[USART2] RX queue overrun: frame discarded\r\n");
    }
}

bool AppControl_GetSnapshot(AppControl_Snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }

    taskENTER_CRITICAL();
    *snapshot = s_snapshot;
    taskEXIT_CRITICAL();

    return (snapshot->valid != 0U);
}
