#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "motor_bts.h"
#include "vehicle_control.h"

static TickType_t s_tick;
static int32_t s_motor_command[MOTOR_BTS_MOTOR_COUNT];
static uint8_t s_motor_braking[MOTOR_BTS_MOTOR_COUNT];

TickType_t xTaskGetTickCount(void)
{
    return s_tick;
}

void MotorBts_StopOne(MOTOR_INDEX_t motor)
{
    s_motor_command[motor] = 0;
    s_motor_braking[motor] = 0U;
}

void MotorBts_BrakeOne(MOTOR_INDEX_t motor)
{
    s_motor_command[motor] = 0;
    s_motor_braking[motor] = 1U;
}

void MotorBts_RunCommand(MOTOR_INDEX_t motor, int32_t command_permille)
{
    s_motor_command[motor] = command_permille;
    s_motor_braking[motor] = 0U;
}

void MotorBts_EmergencyDisableAll(void)
{
    uint32_t motor;

    for (motor = 0U; motor < MOTOR_BTS_MOTOR_COUNT; ++motor)
    {
        s_motor_command[motor] = 0;
        s_motor_braking[motor] = 0U;
    }
}

uint8_t Current_IsSnapshotFresh(const CurrentSense_Snapshot_t *snapshot,
                                uint32_t now_ms,
                                uint32_t timeout_ms)
{
    if (snapshot == NULL || snapshot->generation == 0U)
    {
        return 0U;
    }

    return ((uint32_t)(now_ms - snapshot->timestamp_ms) <= timeout_ms) ?
           1U : 0U;
}

void Usart6Log_VehicleStop(void)
{
}

static void Fail(const char *message)
{
    fprintf(stderr,
            "%s at tick=%lu commands=(%ld,%ld,%ld,%ld) "
            "brake=(%u,%u,%u,%u)\n",
            message,
            (unsigned long)s_tick,
            (long)s_motor_command[0],
            (long)s_motor_command[1],
            (long)s_motor_command[2],
            (long)s_motor_command[3],
            (unsigned int)s_motor_braking[0],
            (unsigned int)s_motor_braking[1],
            (unsigned int)s_motor_braking[2],
            (unsigned int)s_motor_braking[3]);
    exit(EXIT_FAILURE);
}

static uint8_t AllMotorsBraking(void)
{
    return (uint8_t)((s_motor_braking[0] != 0U) &&
                     (s_motor_braking[1] != 0U) &&
                     (s_motor_braking[2] != 0U) &&
                     (s_motor_braking[3] != 0U));
}

static void ExpectEqualForward(void)
{
    if ((s_motor_command[0] != s_motor_command[1]) ||
        (s_motor_command[0] != s_motor_command[2]) ||
        (s_motor_command[0] != s_motor_command[3]) ||
        (s_motor_command[0] < 0))
    {
        Fail("forward commands are not equal");
    }
}

static void ExpectSymmetricPivot(void)
{
    if ((s_motor_command[0] != s_motor_command[1]) ||
        (s_motor_command[2] != s_motor_command[3]) ||
        (s_motor_command[0] != -s_motor_command[2]) ||
        (s_motor_command[0] < 0) ||
        (s_motor_command[0] > 350))
    {
        Fail("left pivot commands are not symmetric/limited");
    }
}

static void Step(VehicleControl_Input_t *input)
{
    ++s_tick;
    VehicleControl_Update(input);
}

static void SetValidCurrentFrame(VehicleControl_Input_t *input,
                                 int32_t control_ma,
                                 int32_t fast_ma)
{
    uint32_t motor;

    input->current.governor_enabled = 1U;
    input->current.snapshot.valid_mask = CURRENT_VALID_ALL;
    input->current.snapshot.timestamp_ms = s_tick + 1U;
    input->current.snapshot.generation++;

    for (motor = 0U; motor < CURRENT_MOTOR_COUNT; ++motor)
    {
        input->current.limits[motor].soft_limit_ma = 10000U;
        input->current.limits[motor].hard_limit_ma = 20000U;
        input->current.snapshot.motor[motor].current_control_ma = control_ma;
        input->current.snapshot.motor[motor].current_fast_ma = fast_ma;
    }
}

static uint8_t AllMotorCommandsZero(void)
{
    uint32_t motor;

    for (motor = 0U; motor < MOTOR_BTS_MOTOR_COUNT; ++motor)
    {
        if (s_motor_command[motor] != 0)
        {
            return 0U;
        }
    }
    return 1U;
}

int main(void)
{
    VehicleControl_Input_t input = {0};
    uint32_t count;

    input.app.valid = 1U;
    input.app.connected = 1U;
    input.app.fresh = 1U;

    VehicleControl_Init();

    input.app.left_permille = 1000;
    input.app.right_permille = 1000;
    VehicleControl_Update(&input);
    for (count = 0U; count < 500U; ++count)
    {
        Step(&input);
        ExpectEqualForward();
    }
    if (s_motor_command[0] != 1000)
    {
        Fail("normal acceleration did not reach 100 percent");
    }

    /* One overloaded motor must reduce all four commands by one common scale. */
    for (count = 0U; count < 100U; ++count)
    {
        SetValidCurrentFrame(&input, 5000, 5000);
        input.current.snapshot.motor[0].current_control_ma = 15000;
        Step(&input);
        ExpectEqualForward();
    }
    if (s_motor_command[0] <= 0 || s_motor_command[0] >= 1000)
    {
        Fail("current governor did not reduce all four motors");
    }

    input.current.governor_enabled = 0U;
    for (count = 0U; count < 5U; ++count)
    {
        Step(&input);
        ExpectEqualForward();
    }
    if (s_motor_command[0] != 1000)
    {
        Fail("disabled current governor changed the original control path");
    }

    input.app.left_permille = -1000;
    input.app.right_permille = 1000;
    for (count = 0U; count < 800U; ++count)
    {
        Step(&input);
        if (AllMotorsBraking() != 0U)
        {
            break;
        }
        ExpectEqualForward();
    }
    if (AllMotorsBraking() == 0U)
    {
        Fail("pivot entry did not brake all motors together");
    }

    for (count = 0U; count < 600U; ++count)
    {
        Step(&input);
        if (s_motor_command[0] != 0)
        {
            ExpectSymmetricPivot();
            break;
        }
    }
    if (s_motor_command[0] == 0)
    {
        Fail("pivot did not start after the common brake interval");
    }

    for (count = 0U; count < 500U; ++count)
    {
        Step(&input);
        ExpectSymmetricPivot();
    }
    if (s_motor_command[0] != 350)
    {
        Fail("pivot output was not limited to 35 percent");
    }

    input.app.left_permille = 1000;
    input.app.right_permille = 1000;
    for (count = 0U; count < 400U; ++count)
    {
        Step(&input);
        if (AllMotorsBraking() != 0U)
        {
            break;
        }
        ExpectSymmetricPivot();
    }
    if (AllMotorsBraking() == 0U)
    {
        Fail("pivot exit did not brake all motors together");
    }

    for (count = 0U; count < 300U; ++count)
    {
        Step(&input);
        if (s_motor_command[0] != 0)
        {
            ExpectEqualForward();
            break;
        }
    }
    if (s_motor_command[0] == 0)
    {
        Fail("normal drive did not resume after pivot exit");
    }

    /* Two distinct hard-current snapshots latch an emergency stop. */
    input.app.left_permille = 1000;
    input.app.right_permille = 1000;
    input.current.governor_enabled = 0U;
    VehicleControl_Init();
    for (count = 0U; count < 500U; ++count)
    {
        Step(&input);
    }
    SetValidCurrentFrame(&input, 5000, 25000);
    Step(&input);
    SetValidCurrentFrame(&input, 5000, 25000);
    Step(&input);
    if (AllMotorCommandsZero() == 0U)
    {
        Fail("hard current fault did not emergency-stop all motors");
    }

    puts("vehicle_control: pivot and current integration tests passed");
    return EXIT_SUCCESS;
}
