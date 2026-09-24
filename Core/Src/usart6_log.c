#include "usart6_log.h"
#include <stddef.h>
#include <stdint.h>
#include "main.h"
#include "stm32f4xx_ll_usart.h"

#define USART6_LOG_BUFFER_SIZE  2048U

static uint8_t s_log_buffer[USART6_LOG_BUFFER_SIZE];
static volatile uint16_t s_log_head;
static volatile uint16_t s_log_tail;

static void Usart6Log_EnqueueChar(char character)
{
    const uint16_t next_head = (uint16_t)((s_log_head + 1U) %
                                          USART6_LOG_BUFFER_SIZE);

    if (next_head != s_log_tail)
    {
        s_log_buffer[s_log_head] = (uint8_t)character;
        s_log_head = next_head;
        LL_USART_EnableIT_TXE(USART6);
    }
    /* If full, drop the character instead of blocking control reception. */
}

static void Usart6Log_WriteChar(char character)
{
    Usart6Log_EnqueueChar(character);
}

static void Usart6Log_WriteUnsigned(uint32_t value)
{
    char digits[10U];
    uint32_t count = 0U;

    if (value == 0U)
    {
        Usart6Log_WriteChar('0');
        return;
    }

    while (value != 0U)
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count > 0U)
    {
        Usart6Log_WriteChar(digits[--count]);
    }
}

static void Usart6Log_WriteSigned(int32_t value)
{
    if (value < 0)
    {
        Usart6Log_WriteChar('-');
        /* Convert through int64_t so INT32_MIN is handled safely. */
        Usart6Log_WriteUnsigned((uint32_t)(-(int64_t)value));
    }
    else
    {
        Usart6Log_WriteUnsigned((uint32_t)value);
    }
}

//                                       int32_t right,
//                                       uint8_t brake)
// {
//     if (brake != 0U)
//     {
//         Usart6Log_Write("BRAKE");
//     }
//     else if ((left == 0) && (right == 0))
//     {
//         Usart6Log_Write("STOP");
//     }
//     else if ((left > 0) && (right > 0))
//     {
//         if (left == right)
//         {
//             Usart6Log_Write("FORWARD");
//         }
//         else if (left < right)
//         {
//             Usart6Log_Write("LEFT");
//         }
//         else
//         {
//             Usart6Log_Write("RIGHT");
//         }
//     }
//     else if ((left < 0) && (right < 0))
//     {
//         if (left == right)
//         {
//             Usart6Log_Write("BACKWARD");
//         }
//         else if (left < right)
//         {
//             Usart6Log_Write("LEFT");
//         }
//         else
//         {
//             Usart6Log_Write("RIGHT");
//         }
//     }
//     else if ((left < 0) && (right > 0) )
//     {
//         Usart6Log_Write("ROTATE_LEFT");
//     }
//     else if ((left > 0) && (right < 0) )
//     {
//         Usart6Log_Write("ROTATE_RIGHT");
//     }
// }

void Usart6Log_Init(void)
{
    s_log_head = 0U;
    s_log_tail = 0U;

    LL_USART_DisableIT_TXE(USART6);
    NVIC_SetPriority(USART6_IRQn,
                     NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                          14U,
                                          0U));
    NVIC_ClearPendingIRQ(USART6_IRQn);
    NVIC_EnableIRQ(USART6_IRQn);

    Usart6Log_Write("USART6 LOG READY\r\n");
}

void Usart6Log_Process(void)
{
    /* The TXE interrupt drains the ring buffer at the UART's full rate. */
    if (s_log_head != s_log_tail)
    {
        LL_USART_EnableIT_TXE(USART6);
    }
}

void Usart6Log_IrqHandler(void)
{
    if ((LL_USART_IsEnabledIT_TXE(USART6) == 0U) ||
        (LL_USART_IsActiveFlag_TXE(USART6) == 0U))
    {
        return;
    }

    if (s_log_head == s_log_tail)
    {
        LL_USART_DisableIT_TXE(USART6);
        return;
    }

    LL_USART_TransmitData8(USART6, s_log_buffer[s_log_tail]);
    s_log_tail = (uint16_t)((s_log_tail + 1U) % USART6_LOG_BUFFER_SIZE);

    if (s_log_head == s_log_tail)
    {
        LL_USART_DisableIT_TXE(USART6);
    }
}

void Usart6Log_Write(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    while (*text != '\0')
    {
        Usart6Log_WriteChar(*text++);
    }
}

void Usart6Log_CurrentSnapshot(
    const CurrentSense_Snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }
    if ((snapshot->valid_mask & 0x0FU) != 0x0FU)
    {
        Usart6Log_Write("CURRENT INVALID\r\n");
        return;
    }
    Usart6Log_Write("M1 = ");
    Usart6Log_WriteSigned(snapshot->filtered_ma[CURRENT_MOTOR_1]);
    Usart6Log_Write("mA\r ");
    Usart6Log_Write("M2 = ");
    Usart6Log_WriteSigned(snapshot->filtered_ma[CURRENT_MOTOR_2]);
    Usart6Log_Write("mA\r ");
    Usart6Log_Write("M3 = ");
    Usart6Log_WriteSigned(snapshot->filtered_ma[CURRENT_MOTOR_3]);
    Usart6Log_Write("mA\r ");
    Usart6Log_Write("M4 = ");
    Usart6Log_WriteSigned(snapshot->filtered_ma[CURRENT_MOTOR_4]);
    Usart6Log_Write("mA\r\n");
}
