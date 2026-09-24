#include "vehicle_control.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "differential_control.h"
#include "motor_bts.h"
#include "task.h"
#include "usart6_log.h"

#define VEHICLE_CONTROL_DYNAMIC_BRAKE_MS          100U
#define VEHICLE_CONTROL_MAX_PERCENT               100
#define VEHICLE_CONTROL_ACCEL_PERCENT_S           500U
#define VEHICLE_CONTROL_DECEL_PERCENT_S           800U
#define VEHICLE_CONTROL_MAX_DT_MS                  100U
#define VEHICLE_CONTROL_MILLIPERCENT_SCALE         1000

#define VEHICLE_CONTROL_PIVOT_MAX_PERCENT           100U
#define VEHICLE_CONTROL_PIVOT_ACCEL_PERCENT_S       80U
#define VEHICLE_CONTROL_PIVOT_DECEL_PERCENT_S      160U
#define VEHICLE_CONTROL_PIVOT_TRANSITION_DECEL_S   150U
#define VEHICLE_CONTROL_CALIBRATION_SCALE         1000U

typedef enum
{
    VEHICLE_MOTOR_DRIVE = 0,
    VEHICLE_MOTOR_RAMP_TO_ZERO,
    VEHICLE_MOTOR_BRAKE_WAIT
} VehicleControl_MotorMode_t;

typedef struct
{
    int32_t ramped_millipercent;
    VehicleControl_MotorMode_t mode;
    TickType_t brake_start_tick;
} VehicleControl_MotorState_t;

/* [ADDED: SYMMETRIC PIVOT TRANSITION]
 * Every transition into, out of, or between pivot directions first brings all
 * four logical motor commands to zero and brakes them together.  This avoids
 * one side continuing at high speed while the opposite side reverses.
 */
typedef enum
{
    VEHICLE_CHASSIS_NORMAL = 0,
    VEHICLE_CHASSIS_PIVOT_RAMP_TO_ZERO,
    VEHICLE_CHASSIS_PIVOT_BRAKE_WAIT,
    VEHICLE_CHASSIS_PIVOT_DRIVE
} VehicleControl_ChassisMode_t;

/* [ADDED: FOUR-MOTOR PWM CALIBRATION]
 * Gain 1000 = 1.000 (no correction).  Tune forward/reverse independently
 * after measuring wheel speed.  Reduce the faster motors towards the slowest
 * motor instead of blindly boosting a weak or mechanically jammed motor.
 *
 * min_*_percent compensates a measured motor dead-zone.  Leave it at zero
 * until a safe starting PWM has been measured with the wheels lifted.
 */
typedef struct
{
    uint16_t forward_gain_permille;
    uint16_t reverse_gain_permille;
    uint8_t min_forward_percent;
    uint8_t min_reverse_percent;
} VehicleControl_MotorCalibration_t;

static const VehicleControl_MotorCalibration_t
    s_motor_calibration[MOTOR_BTS_MOTOR_COUNT] =
{
    /* Motor   forward gain  reverse gain  min forward  min reverse */
    /* M1 */ {1000U, 1000U, 0U, 0U },
    /* M2 */ {1000U, 1000U, 0U, 0U },
    /* M3 */ {1000U, 1000U, 0U, 0U },
    /* M4 */ {1000U, 1000U, 0U, 0U }
};

static uint8_t s_stop_logged;
static VehicleControl_MotorState_t s_motor_states[MOTOR_BTS_MOTOR_COUNT];
static TickType_t s_last_update_tick;
static uint8_t s_timing_initialized;
static VehicleControl_ChassisMode_t s_chassis_mode;
static TickType_t s_chassis_brake_start_tick;
static int8_t s_active_pivot_sign;

static int32_t VehicleControl_ClampPercent(int32_t percent)
{
    if (percent > VEHICLE_CONTROL_MAX_PERCENT)
    {
        return VEHICLE_CONTROL_MAX_PERCENT;
    }

    if (percent < -VEHICLE_CONTROL_MAX_PERCENT)
    {
        return -VEHICLE_CONTROL_MAX_PERCENT;
    }

    return percent;
}

static int32_t VehicleControl_PercentToPermille(int32_t percent)
{
    return VehicleControl_ClampPercent(percent) * 10;
}

static int32_t VehicleControl_Abs(int32_t value)
{
    return (value < 0) ? -value : value;
}

static int8_t VehicleControl_Sign(int32_t value)
{
    return (value > 0) ? 1 : ((value < 0) ? -1 : 0);
}

static int32_t VehicleControl_SlewMillipercent(int32_t current,
                                               int32_t target,
                                               uint32_t rate_percent_s,
                                               uint32_t dt_ms)
{
    int32_t difference;
    uint32_t step_millipercent;

    difference = target - current;
    if ((difference == 0) || (dt_ms == 0U))
    {
        return current;
    }

    step_millipercent = rate_percent_s * dt_ms;

    if (difference > (int32_t)step_millipercent)
    {
        return current + (int32_t)step_millipercent;
    }

    if (difference < -(int32_t)step_millipercent)
    {
        return current - (int32_t)step_millipercent;
    }

    return target;
}

static int32_t VehicleControl_MillipercentToPercent(int32_t value)
{
    if (value >= 0)
    {
        return (value + (VEHICLE_CONTROL_MILLIPERCENT_SCALE / 2)) /
               VEHICLE_CONTROL_MILLIPERCENT_SCALE;
    }

    return -((-value + (VEHICLE_CONTROL_MILLIPERCENT_SCALE / 2)) /
             VEHICLE_CONTROL_MILLIPERCENT_SCALE);
}

static void VehicleControl_ResetMotor(VehicleControl_MotorState_t *motor)
{
    if (motor == NULL)
    {
        return;
    }

    motor->ramped_millipercent = 0;
    motor->mode = VEHICLE_MOTOR_DRIVE;
    motor->brake_start_tick = 0;
}

static uint32_t VehicleControl_GetDeltaMs(TickType_t now)
{
    uint32_t dt_ms;

    if (s_timing_initialized == 0U)
    {
        s_last_update_tick = now;
        s_timing_initialized = 1U;
        return 0U;
    }

    dt_ms = (uint32_t)((now - s_last_update_tick) *
                       (TickType_t)portTICK_PERIOD_MS);
    s_last_update_tick = now;

    if (dt_ms > VEHICLE_CONTROL_MAX_DT_MS)
    {
        /* Do not convert a delayed task into one large duty-cycle jump. */
        dt_ms = VEHICLE_CONTROL_MAX_DT_MS;
    }

    return dt_ms;
}

static uint32_t VehicleControl_SelectRate(int32_t current,
                                          int32_t target,
                                          uint32_t accel_rate_percent_s,
                                          uint32_t decel_rate_percent_s)
{
    if (VehicleControl_Abs(target) > VehicleControl_Abs(current))
    {
        return accel_rate_percent_s;
    }

    return decel_rate_percent_s;
}

static int32_t VehicleControl_UpdateMotor(VehicleControl_MotorState_t *motor,
                                          int32_t target_percent,
                                          uint32_t dt_ms,
                                          TickType_t now,
                                          uint32_t accel_rate_percent_s,
                                          uint32_t decel_rate_percent_s,
                                          uint8_t *dynamic_brake)
{
    int32_t target_millipercent;
    int8_t current_direction;
    int8_t target_direction;
    uint32_t rate;

    if ((motor == NULL) || (dynamic_brake == NULL))
    {
        return 0;
    }

    *dynamic_brake = 0U;
    target_percent = VehicleControl_ClampPercent(target_percent);
    target_millipercent = target_percent *
                          VEHICLE_CONTROL_MILLIPERCENT_SCALE;
    current_direction = VehicleControl_Sign(motor->ramped_millipercent);
    target_direction = VehicleControl_Sign(target_millipercent);

    /* A direction request can be cancelled safely before reaching zero. */
    if ((motor->mode == VEHICLE_MOTOR_RAMP_TO_ZERO) &&
        (current_direction != 0) &&
        (target_direction == current_direction))
    {
        motor->mode = VEHICLE_MOTOR_DRIVE;
    }

    if (motor->mode == VEHICLE_MOTOR_BRAKE_WAIT)
    {
        motor->ramped_millipercent = 0;

        if (target_direction == 0)
        {
            /* Neutral releases the active brake into the normal coast state. */
            motor->mode = VEHICLE_MOTOR_DRIVE;
            return 0;
        }

        if ((TickType_t)(now - motor->brake_start_tick) <
            pdMS_TO_TICKS(VEHICLE_CONTROL_DYNAMIC_BRAKE_MS))
        {
            *dynamic_brake = 1U;
            return 0;
        }

        /* Release at zero; the new direction ramps up on the next update. */
        motor->mode = VEHICLE_MOTOR_DRIVE;
        return 0;
    }

    if (motor->mode == VEHICLE_MOTOR_RAMP_TO_ZERO)
    {
        motor->ramped_millipercent =
            VehicleControl_SlewMillipercent(motor->ramped_millipercent,
                                             0,
                                             decel_rate_percent_s,
                                             dt_ms);

        if (motor->ramped_millipercent == 0)
        {
            if (target_direction != 0)
            {
                motor->mode = VEHICLE_MOTOR_BRAKE_WAIT;
                motor->brake_start_tick = now;
                *dynamic_brake = 1U;
            }
            else
            {
                motor->mode = VEHICLE_MOTOR_DRIVE;
            }
        }

        return VehicleControl_MillipercentToPercent(
            motor->ramped_millipercent);
    }

    if ((current_direction != 0) &&
        (target_direction != 0) &&
        (current_direction != target_direction))
    {
        /* Opposite signs must pass through zero and the brake-wait state. */
        motor->mode = VEHICLE_MOTOR_RAMP_TO_ZERO;
        motor->ramped_millipercent =
            VehicleControl_SlewMillipercent(motor->ramped_millipercent,
                                             0,
                                             decel_rate_percent_s,
                                             dt_ms);

        if (motor->ramped_millipercent == 0)
        {
            motor->mode = VEHICLE_MOTOR_BRAKE_WAIT;
            motor->brake_start_tick = now;
            *dynamic_brake = 1U;
        }

        return VehicleControl_MillipercentToPercent(
            motor->ramped_millipercent);
    }

    rate = VehicleControl_SelectRate(motor->ramped_millipercent,
                                     target_millipercent,
                                     accel_rate_percent_s,
                                     decel_rate_percent_s);
    motor->ramped_millipercent =
        VehicleControl_SlewMillipercent(motor->ramped_millipercent,
                                         target_millipercent,
                                         rate,
                                         dt_ms);

    return VehicleControl_MillipercentToPercent(motor->ramped_millipercent);
}

/* [ADDED: PIVOT CONTROL] Return the sign of the right-side pivot command. */
static int8_t VehicleControl_GetPivotSign(
    const DifferentialControl_Command_t *command)
{
    int8_t right_sign;
    int8_t left_sign;

    if (command == NULL)
    {
        return 0;
    }

    right_sign = VehicleControl_Sign(command->m1_percent);
    left_sign = VehicleControl_Sign(command->m3_percent);

    if ((right_sign == 0) || (left_sign == 0) ||
        (right_sign == left_sign))
    {
        return 0;
    }

    return right_sign;
}

/* [ADDED: PIVOT CONTROL]
 * Force equal magnitudes on both sides.  If the app sends slightly unequal
 * opposite commands, use the smaller magnitude so neither side is boosted.
 */
static void VehicleControl_LimitPivotCommand(
    DifferentialControl_Command_t *command)
{
    int8_t right_sign;
    int32_t right_magnitude;
    int32_t left_magnitude;
    int32_t pivot_magnitude;

    right_sign = VehicleControl_GetPivotSign(command);
    if (right_sign == 0)
    {
        return;
    }

    right_magnitude = VehicleControl_Abs(command->m1_percent);
    left_magnitude = VehicleControl_Abs(command->m3_percent);
    pivot_magnitude = (right_magnitude < left_magnitude) ?
                      right_magnitude : left_magnitude;

    if (pivot_magnitude > VEHICLE_CONTROL_PIVOT_MAX_PERCENT)
    {
        pivot_magnitude = VEHICLE_CONTROL_PIVOT_MAX_PERCENT;
    }

    command->m1_percent = (int32_t)right_sign * pivot_magnitude;
    command->m2_percent = command->m1_percent;
    command->m3_percent = -command->m1_percent;
    command->m4_percent = command->m3_percent;
}

static void VehicleControl_MakeZeroCommand(
    DifferentialControl_Command_t *command)
{
    if (command == NULL)
    {
        return;
    }

    command->m1_percent = 0;
    command->m2_percent = 0;
    command->m3_percent = 0;
    command->m4_percent = 0;
    command->direction = DIFFERENTIAL_DIRECTION_STOP;
}

static uint8_t VehicleControl_AllMotorsAtZero(void)
{
    uint32_t index;

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        if (s_motor_states[index].ramped_millipercent != 0)
        {
            return 0U;
        }
    }

    return 1U;
}

static void VehicleControl_BrakeAll(void)
{
    uint32_t index;
    
    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        MotorBts_BrakeOne((MOTOR_INDEX_t)index);
    }
}

static void VehicleControl_ApplyBrake(void){
    uint32_t index;
    VehicleControl_BrakeAll();
    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index){
        VehicleControl_ResetMotor(&s_motor_states[index]);
    }
    s_timing_initialized = 0U;
    s_chassis_mode = VEHICLE_CHASSIS_NORMAL;
    s_chassis_brake_start_tick = 0;
    s_active_pivot_sign = 0;
}
static int32_t VehicleControl_ApplyMotorCalibration(MOTOR_INDEX_t motor,
                                                    int32_t percent,
                                                    int32_t output_limit)
{
    const VehicleControl_MotorCalibration_t *calibration;
    uint32_t gain;
    uint32_t minimum;
    uint32_t magnitude;

    if (((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT) || (percent == 0))
    {
        return 0;
    }

    calibration = &s_motor_calibration[motor];
    if (percent > 0)
    {
        gain = calibration->forward_gain_permille;
        minimum = calibration->min_forward_percent;
    }
    else
    {
        gain = calibration->reverse_gain_permille;
        minimum = calibration->min_reverse_percent;
    }

    magnitude = (uint32_t)VehicleControl_Abs(percent);
    magnitude = ((magnitude * gain) +
                 (VEHICLE_CONTROL_CALIBRATION_SCALE / 2U)) /
                VEHICLE_CONTROL_CALIBRATION_SCALE;

    if ((magnitude != 0U) && (magnitude < minimum))
    {
        magnitude = minimum;
    }

    if (output_limit > VEHICLE_CONTROL_MAX_PERCENT)
    {
        output_limit = VEHICLE_CONTROL_MAX_PERCENT;
    }
    if (output_limit < 0)
    {
        output_limit = 0;
    }
    if (magnitude > (uint32_t)output_limit)
    {
        magnitude = (uint32_t)output_limit;
    }

    /* Calibration may reduce a faster motor but must never boost command. */
    if (magnitude > (uint32_t)VehicleControl_Abs(percent))
    {
        magnitude = (uint32_t)VehicleControl_Abs(percent);
    }

    return (percent > 0) ? (int32_t)magnitude : -(int32_t)magnitude;
}

static int32_t VehicleControl_GetMotorTarget(
    const DifferentialControl_Command_t *command,
    MOTOR_INDEX_t motor)
{
    if (command == NULL)
    {
        return 0;
    }

    switch (motor)
    {
        case MOTOR_BTS_M1:
            return command->m1_percent;
        case MOTOR_BTS_M2:
            return command->m2_percent;
        case MOTOR_BTS_M3:
            return command->m3_percent;
        case MOTOR_BTS_M4:
            return command->m4_percent;
        default:
            return 0;
    }
}

static void VehicleControl_SafeReverse(
    const DifferentialControl_Command_t *command,
    uint32_t accel_rate_percent_s,
    uint32_t decel_rate_percent_s,
    int32_t output_limit)
{
    TickType_t now;
    uint32_t dt_ms;
    uint32_t index;
    int32_t output_percent[MOTOR_BTS_MOTOR_COUNT];
    uint8_t dynamic_brake[MOTOR_BTS_MOTOR_COUNT];

    if (command == NULL)
    {
        return;
    }

    now = xTaskGetTickCount();
    dt_ms = VehicleControl_GetDeltaMs(now);

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        MOTOR_INDEX_t motor = (MOTOR_INDEX_t)index;

        output_percent[index] = VehicleControl_UpdateMotor(
            &s_motor_states[index],
            VehicleControl_GetMotorTarget(command, motor),
            dt_ms,
            now,
            accel_rate_percent_s,
            decel_rate_percent_s,
            &dynamic_brake[index]);
    }

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        MOTOR_INDEX_t motor = (MOTOR_INDEX_t)index;
        int32_t calibrated_percent;

        if (dynamic_brake[index] != 0U)
        {
            MotorBts_BrakeOne(motor);
        }
        else
        {
            calibrated_percent = VehicleControl_ApplyMotorCalibration(
                motor,
                output_percent[index],
                output_limit);
            MotorBts_RunCommand(
                motor,
                VehicleControl_PercentToPermille(calibrated_percent));
        }
    }
}

/* [ADDED: SYMMETRIC PIVOT TRANSITION]
 * This chassis-level state machine surrounds the existing per-motor safe
 * reversal state machine.  All four motors decelerate and brake together at a
 * pivot boundary; only then are symmetric, limited pivot commands released.
 */
static void VehicleControl_UpdateChassis(
    const DifferentialControl_Command_t *requested_command)
{
    DifferentialControl_Command_t command;
    DifferentialControl_Command_t zero_command;
    TickType_t now;
    int8_t requested_pivot_sign;

    if (requested_command == NULL)
    {
        return;
    }

    command = *requested_command;
    requested_pivot_sign = VehicleControl_GetPivotSign(&command);
    VehicleControl_LimitPivotCommand(&command);
    now = xTaskGetTickCount();

    if (s_chassis_mode == VEHICLE_CHASSIS_NORMAL)
    {
        if (requested_pivot_sign == 0)
        {
            VehicleControl_SafeReverse(&command,
                                       VEHICLE_CONTROL_ACCEL_PERCENT_S,
                                       VEHICLE_CONTROL_DECEL_PERCENT_S,
                                       VEHICLE_CONTROL_MAX_PERCENT);
            return;
        }

        s_chassis_mode = VEHICLE_CHASSIS_PIVOT_RAMP_TO_ZERO;
        s_active_pivot_sign = 0;
    }

    if (s_chassis_mode == VEHICLE_CHASSIS_PIVOT_DRIVE)
    {
        if ((requested_pivot_sign != 0) &&
            (requested_pivot_sign == s_active_pivot_sign))
        {
            VehicleControl_SafeReverse(&command,
                                       VEHICLE_CONTROL_PIVOT_ACCEL_PERCENT_S,
                                       VEHICLE_CONTROL_PIVOT_DECEL_PERCENT_S,
                                       VEHICLE_CONTROL_PIVOT_MAX_PERCENT);
            return;
        }

        /* Leaving pivot or reversing pivot direction: stop all four first. */
        s_chassis_mode = VEHICLE_CHASSIS_PIVOT_RAMP_TO_ZERO;
        s_active_pivot_sign = 0;
    }

    VehicleControl_MakeZeroCommand(&zero_command);

    if (s_chassis_mode == VEHICLE_CHASSIS_PIVOT_RAMP_TO_ZERO)
    {
        VehicleControl_SafeReverse(&zero_command,
                                   VEHICLE_CONTROL_ACCEL_PERCENT_S,
                                   VEHICLE_CONTROL_PIVOT_TRANSITION_DECEL_S,
                                   VEHICLE_CONTROL_MAX_PERCENT);

        if (VehicleControl_AllMotorsAtZero() != 0U)
        {
            s_chassis_mode = VEHICLE_CHASSIS_PIVOT_BRAKE_WAIT;
            s_chassis_brake_start_tick = now;
            VehicleControl_BrakeAll();
        }
        return;
    }

    if (s_chassis_mode == VEHICLE_CHASSIS_PIVOT_BRAKE_WAIT)
    {
        /* Keep the ramp time base current while the physical outputs brake. */
        VehicleControl_SafeReverse(&zero_command,
                                   VEHICLE_CONTROL_ACCEL_PERCENT_S,
                                   VEHICLE_CONTROL_PIVOT_TRANSITION_DECEL_S,
                                   VEHICLE_CONTROL_MAX_PERCENT);
        VehicleControl_BrakeAll();

        if ((TickType_t)(now - s_chassis_brake_start_tick) <
            pdMS_TO_TICKS(VEHICLE_CONTROL_DYNAMIC_BRAKE_MS))
        {
            return;
        }

        if (requested_pivot_sign != 0)
        {
            s_active_pivot_sign = requested_pivot_sign;
            s_chassis_mode = VEHICLE_CHASSIS_PIVOT_DRIVE;
        }
        else
        {
            s_active_pivot_sign = 0;
            s_chassis_mode = VEHICLE_CHASSIS_NORMAL;
        }
    }
}

void VehicleControl_Stop(void)
{
    uint32_t index;

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        MotorBts_StopOne((MOTOR_INDEX_t)index);
        VehicleControl_ResetMotor(&s_motor_states[index]);
    }

    s_timing_initialized = 0U;
    s_chassis_mode = VEHICLE_CHASSIS_NORMAL;
    s_chassis_brake_start_tick = 0;
    s_active_pivot_sign = 0;

    if (s_stop_logged == 0U)
    {
        // Usart6Log_VehicleStop();
        s_stop_logged = 1U;
    }
}

void VehicleControl_Init(void)
{
    s_stop_logged = 0U;
    VehicleControl_Stop();
}


void VehicleControl_Update(const VehicleControl_Input_t *input)
{
    DifferentialControl_Command_t command;

    if ((input == NULL) ||
        (input->app.valid == 0U) ||
        (input->app.connected == 0U) ||
        (input->app.fresh == 0U))
    {
        VehicleControl_Stop();
        return;
    }

    if (input->app.brake != 0U)
    {
         VehicleControl_ApplyBrake();
         return;
    }

    s_stop_logged = 0U;

    DifferentialControl_Compute(input->app.left_permille,
                                input->app.right_permille,
                                &command);

    VehicleControl_UpdateChassis(&command);
}
