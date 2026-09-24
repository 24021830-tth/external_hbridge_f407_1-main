/*
 * BTS motor bridge control.
 *
 * The four motors use two PWM inputs each.  For a given motor only one of
 * the two PWM inputs is driven at a time; both INH inputs are active high.
 */
#ifndef MOTOR_BTS_H
#define MOTOR_BTS_H

#include <stdint.h>

#include "stm32f4xx_ll_tim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MOTOR_BTS_M1 = 0,
    MOTOR_BTS_M2,
    MOTOR_BTS_M3,
    MOTOR_BTS_M4,
    MOTOR_BTS_MOTOR_COUNT
} MOTOR_INDEX_t;

typedef enum
{
    MOTOR_BTS_A = 0,
    MOTOR_BTS_B
} MOTOR_DIR_t;

typedef enum
{
    MOTOR_BTS_OUTPUT_COAST = 0,
    MOTOR_BTS_OUTPUT_DRIVE,
    MOTOR_BTS_OUTPUT_BRAKE
} MotorBts_OutputMode_t;

typedef struct
{
    int32_t command_permille;
    MOTOR_DIR_t direction;
    MotorBts_OutputMode_t mode;
    uint32_t transition_generation;
} MotorBts_AppliedState_t;

/* Clear one or more timer compare registers. */
void MotorBts_Stop(TIM_TypeDef *TIMx, uint32_t channels);

/* Initialise the bridge in a safe state with INH inputs disabled. */
void MotorBts_InitSafe(void);

/* Set the same speed for all four motors, preserving their directions. */
void MotorBts_SetSpeed(uint32_t percent);

/* Stop one motor and clear its PWM outputs. */
void MotorBts_StopOne(MOTOR_INDEX_t motor);

/* Run one motor.  A direction change first stops the motor; call again to run. */
void MotorBts_RunOne(MOTOR_INDEX_t motor,
                     MOTOR_DIR_t direction,
                     uint32_t percent);

/* Signed command in the range -1000 .. +1000 (permille). */
void MotorBts_RunCommand(MOTOR_INDEX_t motor, int32_t command_permille);

/* Change the direction state of all motors without applying speed. */
void MotorBts_DIR(MOTOR_DIR_t direction);

/* Run all four motors in the requested direction and speed. */
void MotorBts_Run(MOTOR_DIR_t direction, uint32_t percent);
void MotorBts_BrakeOne(MOTOR_INDEX_t motor);

/* Atomic read-only view used by current-sense observability gating. */
void MotorBts_GetAppliedState(MOTOR_INDEX_t motor,
                              MotorBts_AppliedState_t *state);

/* Idempotent emergency primitive: all CCR=0 and all bridge INH low. */
void MotorBts_EmergencyDisableAll(void);
#ifdef __cplusplus
}
#endif

#endif /* MOTOR_BTS_H */
