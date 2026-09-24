#ifndef CURRENT_SOLVE_H_
#define CURRENT_SOLVE_H_

#include <stdint.h>

#define CURRENT_SOLVE_MOTOR_COUNT 4U

typedef struct
{
    uint16_t raw_adc[CURRENT_SOLVE_MOTOR_COUNT];
    uint16_t average_adc[CURRENT_SOLVE_MOTOR_COUNT];
    uint16_t peak_adc[CURRENT_SOLVE_MOTOR_COUNT];

    int32_t current_ma[CURRENT_SOLVE_MOTOR_COUNT];
    int32_t filtered_ma[CURRENT_SOLVE_MOTOR_COUNT];
    int32_t peak_ma[CURRENT_SOLVE_MOTOR_COUNT];

} CurrentSolve_Result_t;


void CurrentSolve_Init(void);

void CurrentSolve_ProcessBlock(
    const uint16_t *raw,
    uint32_t sample_count,
    CurrentSolve_Result_t *result);


#endif