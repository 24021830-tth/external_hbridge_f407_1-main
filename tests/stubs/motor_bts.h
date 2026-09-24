#ifndef TEST_STUB_MOTOR_BTS_H
#define TEST_STUB_MOTOR_BTS_H

#include <stdint.h>

typedef enum
{
    MOTOR_BTS_M1 = 0,
    MOTOR_BTS_M2,
    MOTOR_BTS_M3,
    MOTOR_BTS_M4,
    MOTOR_BTS_MOTOR_COUNT
} MOTOR_INDEX_t;

void MotorBts_StopOne(MOTOR_INDEX_t motor);
void MotorBts_BrakeOne(MOTOR_INDEX_t motor);
void MotorBts_RunCommand(MOTOR_INDEX_t motor, int32_t command_permille);
void MotorBts_EmergencyDisableAll(void);

#endif /* TEST_STUB_MOTOR_BTS_H */
