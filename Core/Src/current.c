#include "current.h"
#include "current_solve.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_ll_adc.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_tim.h"
#include <string.h>

#define CURRENT_SAMPLE_PER_BLOCK     53U
#define CURRENT_DMA_BLOCK_COUNT      2U
#define CURRENT_DMA_HALF_SIZE       (CURRENT_SAMPLE_PER_BLOCK * CURRENT_MOTOR_COUNT)
#define CURRENT_DMA_BUFFER_SIZE     (CURRENT_DMA_HALF_SIZE * CURRENT_DMA_BLOCK_COUNT)
#define CURRENT_EVENT_FIRST_HALF    (1UL << 0)
#define CURRENT_EVENT_SECOND_HALF    (1UL << 1)
#define CURRENT_EVENT_ALL           (CURRENT_EVENT_FIRST_HALF |  CURRENT_EVENT_SECOND_HALF)
static volatile uint16_t s_adc_dma_buffer[CURRENT_DMA_BUFFER_SIZE];
static TaskHandle_t s_current_task_handle;
static CurrentSense_Snapshot_t s_snapshot;
static uint32_t s_sequence;
static uint8_t s_running;

#if CURRENT_SOLVE_MOTOR_COUNT != CURRENT_MOTOR_COUNT
#error "Current motor count mismatch"
#endif

static void Current_ClearDMAFlags(void)
{
    LL_DMA_ClearFlag_FE4(DMA2);
    LL_DMA_ClearFlag_DME4(DMA2);
    LL_DMA_ClearFlag_TE4(DMA2);
    LL_DMA_ClearFlag_HT4(DMA2);
    LL_DMA_ClearFlag_TC4(DMA2);
}

void Current_Init(void)
{
    LL_TIM_DisableCounter(TIM8);
    if (LL_ADC_IsEnabled(ADC1) != 0U)
    {
        LL_ADC_Disable(ADC1);
    }
    NVIC_DisableIRQ(DMA2_Stream4_IRQn);
    NVIC_ClearPendingIRQ(DMA2_Stream4_IRQn);
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_4);
    while (LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_4) != 0U)
    {
    }
    LL_DMA_SetPeriphAddress(DMA2,
                            LL_DMA_STREAM_4,
                            (uint32_t)&ADC1->DR);
    LL_DMA_SetMemoryAddress(DMA2,
                            LL_DMA_STREAM_4,
                            (uint32_t)&s_adc_dma_buffer[0]);
    LL_DMA_SetDataLength(DMA2,
                         LL_DMA_STREAM_4,
                         CURRENT_DMA_BUFFER_SIZE);
    Current_ClearDMAFlags();
    memset((void *)s_adc_dma_buffer, 0, sizeof(s_adc_dma_buffer));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_current_task_handle = NULL;
    s_sequence = 0U;
    s_running = 0U;
    CurrentSolve_Init();
}

void Current_Start(void)
{
    if ((s_running != 0U) ||
        (s_current_task_handle == NULL))
    {
        return;
    }
    LL_TIM_DisableCounter(TIM8);
    LL_TIM_SetCounter(TIM8, 0U);
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_4);
    while (LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_4) != 0U)
    {
    }
    Current_ClearDMAFlags();
    LL_DMA_SetPeriphAddress(DMA2,
                            LL_DMA_STREAM_4,
                            (uint32_t)&ADC1->DR);
    LL_DMA_SetMemoryAddress(DMA2,
                            LL_DMA_STREAM_4,
                            (uint32_t)&s_adc_dma_buffer[0]);
    LL_DMA_SetDataLength(DMA2,
                         LL_DMA_STREAM_4,
                         CURRENT_DMA_BUFFER_SIZE);
    LL_DMA_EnableIT_HT(DMA2, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_DME(DMA2, LL_DMA_STREAM_4);
    LL_DMA_EnableIT_FE(DMA2, LL_DMA_STREAM_4);
    NVIC_ClearPendingIRQ(DMA2_Stream4_IRQn);
    NVIC_EnableIRQ(DMA2_Stream4_IRQn);
    LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_4);
    LL_ADC_ClearFlag_OVR(ADC1);
    if (LL_ADC_IsEnabled(ADC1) == 0U)
    {
        LL_ADC_Enable(ADC1);
    }
    LL_ADC_REG_StartConversionExtTrig(
        ADC1,
        LL_ADC_REG_TRIG_EXT_RISING);
    s_running = 1U;
    LL_TIM_EnableCounter(TIM8);
}

static void Current_ProcessBlock(
    uint32_t half)
{
    uint32_t base;
    CurrentSolve_Result_t solve;
    CurrentSense_Snapshot_t next;

    base = half * CURRENT_DMA_HALF_SIZE;
    CurrentSolve_ProcessBlock((const uint16_t *)&s_adc_dma_buffer[base],
                                 CURRENT_SAMPLE_PER_BLOCK,
                                  &solve);
    memset(&next, 0, sizeof(next));
    memcpy(next.raw_adc, solve.raw_adc, sizeof(next.raw_adc));
    memcpy(next.average_adc, solve.average_adc, sizeof(next.average_adc));
    memcpy(next.peak_adc, solve.peak_adc, sizeof(next.peak_adc));
    memcpy(next.current_ma, solve.current_ma, sizeof(next.current_ma));
    memcpy(next.filtered_ma, solve.filtered_ma, sizeof(next.filtered_ma));
    memcpy(next.peak_ma, solve.peak_ma, sizeof(next.peak_ma));
    next.timestamp_ms = HAL_GetTick();
    next.sequence = ++s_sequence;
    next.valid_mask = 0x0FU;
    taskENTER_CRITICAL();
    s_snapshot = next;
    taskEXIT_CRITICAL();
}

void Current_DMA_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint32_t event = 0U;
    if (LL_DMA_IsActiveFlag_FE4(DMA2) != 0U)
    {
        LL_DMA_ClearFlag_FE4(DMA2);
    }
    if (LL_DMA_IsActiveFlag_DME4(DMA2) != 0U)
    {
        LL_DMA_ClearFlag_DME4(DMA2);
    }
    if (LL_DMA_IsActiveFlag_TE4(DMA2) != 0U)
    {
        LL_DMA_ClearFlag_TE4(DMA2);
    }
    if (LL_DMA_IsActiveFlag_HT4(DMA2) != 0U)
    {
        LL_DMA_ClearFlag_HT4(DMA2);
        event |= CURRENT_EVENT_FIRST_HALF;
    }
    if (LL_DMA_IsActiveFlag_TC4(DMA2) != 0U)
    {
        LL_DMA_ClearFlag_TC4(DMA2);
        event |= CURRENT_EVENT_SECOND_HALF;
    }
    if ((event != 0U) && (s_current_task_handle != NULL))
    {
        xTaskNotifyFromISR(s_current_task_handle, event, eSetBits,
            &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(
        higher_priority_task_woken);
}

void Current_Stop(void)
{
    LL_TIM_DisableCounter(TIM8);
    if (LL_ADC_IsEnabled(ADC1) != 0U)
    {
        LL_ADC_Disable(ADC1);
    }
    NVIC_DisableIRQ(DMA2_Stream4_IRQn);
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_4);
    while (LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_4) != 0U)
    {
    }
    LL_DMA_DisableIT_HT(DMA2, LL_DMA_STREAM_4);
    LL_DMA_DisableIT_TC(DMA2, LL_DMA_STREAM_4);
    LL_DMA_DisableIT_TE(DMA2, LL_DMA_STREAM_4);
    LL_DMA_DisableIT_DME(DMA2, LL_DMA_STREAM_4);
    LL_DMA_DisableIT_FE(DMA2, LL_DMA_STREAM_4);
    Current_ClearDMAFlags();
    s_running = 0U;
    taskENTER_CRITICAL();
    s_snapshot.valid_mask = 0U;
    taskEXIT_CRITICAL();
}

void Current_Task(void *argument)
{
    uint32_t event;
    (void)argument;
    s_current_task_handle = xTaskGetCurrentTaskHandle();
    Current_Start();
    for (;;)
    {
        event = 0U;
        xTaskNotifyWait(0U, CURRENT_EVENT_ALL, &event, portMAX_DELAY);
        if ((event & CURRENT_EVENT_FIRST_HALF) != 0U)
        {
            Current_ProcessBlock(0U);
        }
        if ((event &
             CURRENT_EVENT_SECOND_HALF) != 0U)
        {
            Current_ProcessBlock(1U);
        }
    }
}

void Current_GetSnapshot(CurrentSense_Snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }
    taskENTER_CRITICAL();
    *snapshot = s_snapshot;
    taskEXIT_CRITICAL();
    if (Current_IsSnapshotFresh(
            snapshot,
            CURRENT_SNAPSHOT_TIMEOUT_MS) == 0U)
    {
        snapshot->valid_mask = 0U;
    }
}
uint8_t Current_IsSnapshotFresh(
    const CurrentSense_Snapshot_t *snapshot,
    uint32_t timeout_ms)
{
    if ((snapshot == NULL) ||
        (snapshot->sequence == 0U))
    {
        return 0U;
    }
    return ((uint32_t)(HAL_GetTick() -
                       snapshot->timestamp_ms) <= timeout_ms)
               ? 1U
               : 0U;
}
