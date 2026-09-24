#include "current_solve.h"
#include <stddef.h>
#include <string.h>


/* ================================================================
 * ADC / BTS7960
 * ================================================================ */

#define CURRENT_ADC_MAX_VALUE       4095UL
#define CURRENT_ADC_VREF_MV         3300UL
#define CURRENT_M1_R_IS_OHM         4490UL
#define CURRENT_M2_R_IS_OHM         4454UL
#define CURRENT_M3_R_IS_OHM         4459UL
#define CURRENT_M4_R_IS_OHM         4455UL
#define CURRENT_KILIS               8500UL

#define CURRENT_MOVING_AVG_SIZE     8U //Moving average trên 8 kết quả block gần nhất
#define CURRENT_FILTER_DIVISOR      8L

static int32_t s_ma_history
        [CURRENT_SOLVE_MOTOR_COUNT]
        [CURRENT_MOVING_AVG_SIZE];
static int32_t s_ma_sum[CURRENT_SOLVE_MOTOR_COUNT];
static uint8_t s_ma_index[CURRENT_SOLVE_MOTOR_COUNT];
static uint8_t s_ma_count[CURRENT_SOLVE_MOTOR_COUNT];
static int32_t s_iir_state[CURRENT_SOLVE_MOTOR_COUNT];
static uint8_t s_iir_initialized[CURRENT_SOLVE_MOTOR_COUNT];
static const uint32_t s_r_is_ohm[CURRENT_SOLVE_MOTOR_COUNT] =
{
    CURRENT_M1_R_IS_OHM,
    CURRENT_M2_R_IS_OHM,
    CURRENT_M3_R_IS_OHM,
    CURRENT_M4_R_IS_OHM
};
// static int32_t CurrentSolve_AdcToMilliAmp(uint16_t raw_adc)
// {
//     uint64_t current_ma;
//     current_ma = (uint64_t)raw_adc * (uint64_t)CURRENT_ADC_VREF_MV * (uint64_t)CURRENT_KILIS;
//     current_ma /= (uint64_t)CURRENT_ADC_MAX_VALUE * (uint64_t)CURRENT_R_IS_OHM;
//     return (int32_t)current_ma;
// }

static int32_t CurrentSolve_AdcToMilliAmp(uint32_t motor, uint16_t raw_adc)
{
    uint64_t current_ma;
    if ((motor >= CURRENT_SOLVE_MOTOR_COUNT) ||
        (s_r_is_ohm[motor] == 0U))
    {
        return 0;
    }
    current_ma = (uint64_t)raw_adc *
                 (uint64_t)CURRENT_ADC_VREF_MV *
                 (uint64_t)CURRENT_KILIS;
    current_ma /= (uint64_t)CURRENT_ADC_MAX_VALUE * (uint64_t)s_r_is_ohm[motor];
    return (int32_t)current_ma;
}

/* ================================================================
 * MOVING AVERAGE 8 + IIR
 * ================================================================ */

static int32_t CurrentSolve_Filter(uint32_t motor, int32_t new_current_ma)
{
    uint8_t index;
    int32_t moving_average;
    index = s_ma_index[motor];
    s_ma_sum[motor] -=  s_ma_history[motor][index];
    s_ma_history[motor][index] = new_current_ma;
    s_ma_sum[motor] += new_current_ma;
    index++;
    if (index >= CURRENT_MOVING_AVG_SIZE)
    {
        index = 0U;
    }
    s_ma_index[motor] = index;
    if (s_ma_count[motor] < CURRENT_MOVING_AVG_SIZE)
    {
        s_ma_count[motor]++;
    }
    moving_average = s_ma_sum[motor] / (int32_t)s_ma_count[motor];

    if (s_iir_initialized[motor] == 0U)
    {
        s_iir_state[motor] = moving_average;
        s_iir_initialized[motor] = 1U;
        return moving_average;
    }
    s_iir_state[motor] += (moving_average - s_iir_state[motor]) / CURRENT_FILTER_DIVISOR;
    return s_iir_state[motor];
}
void CurrentSolve_Init(void)
{
    memset(s_ma_history, 0, sizeof(s_ma_history));
    memset(s_ma_sum, 0, sizeof(s_ma_sum));
    memset(s_ma_index, 0, sizeof(s_ma_index));
    memset(s_ma_count, 0, sizeof(s_ma_count));
    memset(s_iir_state, 0, sizeof(s_iir_state));
    memset(s_iir_initialized, 0, sizeof(s_iir_initialized));
}


/* ================================================================
 * PROCESS ONE DMA HALF
 * ================================================================ */

void CurrentSolve_ProcessBlock(const uint16_t *raw,
    uint32_t sample_count,
    CurrentSolve_Result_t *result)
{
    uint32_t sum[CURRENT_SOLVE_MOTOR_COUNT] = {0U, 0U, 0U, 0U};
    uint16_t peak[CURRENT_SOLVE_MOTOR_COUNT] = {0U, 0U, 0U, 0U};
    uint16_t latest[CURRENT_SOLVE_MOTOR_COUNT] = {0U, 0U, 0U, 0U};
    uint32_t sample;
    uint32_t motor;
    if ((result == NULL))
    {
        return;
    }
    memset(result, 0, sizeof(*result));
    if ((raw == NULL) || (sample_count == 0U)){
        return;
    }
    for (sample = 0U; sample < sample_count; sample++)
    {
        uint32_t base;
        base = sample * CURRENT_SOLVE_MOTOR_COUNT;
        for (motor = 0U; motor < CURRENT_SOLVE_MOTOR_COUNT; motor++)
        {
            uint16_t value;
            value = raw[base + motor];
            sum[motor] += value;
            latest[motor] = value;
            if (value > peak[motor])
            {
                peak[motor] = value;
            }
        }
    }
    for (motor = 0U; motor < CURRENT_SOLVE_MOTOR_COUNT; motor++)
    {
        uint16_t average;
        average = (uint16_t)((sum[motor] + (sample_count / 2U)) / sample_count);
        result->raw_adc[motor] = latest[motor];
        result->average_adc[motor] = average;
        result->peak_adc[motor] = peak[motor];
        // result->current_ma[motor] = CurrentSolve_AdcToMilliAmp(average);
        // result->filtered_ma[motor] = CurrentSolve_Filter(motor, result->current_ma[motor]);
        // result->peak_ma[motor] = CurrentSolve_AdcToMilliAmp(peak[motor]);

        result->current_ma[motor] = CurrentSolve_AdcToMilliAmp(motor, average);
        result->filtered_ma[motor] = CurrentSolve_Filter(motor,
                                result->current_ma[motor]);
        result->peak_ma[motor] = CurrentSolve_AdcToMilliAmp(motor,peak[motor]);
    }
}