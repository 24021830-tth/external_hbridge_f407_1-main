#include "differential_control.h"
#include <stddef.h>
#include <stdlib.h>
#define VEHICLE_PI_2                        1.570796326794897f
#define DIFFERENTIAL_CONTROL_MAX_PERMILLE       1000
#define DIFFERENTIAL_CONTROL_PERCENT_SCALE       100
#define DIFFERENTIAL_CONTROL_SPEED_MAX      VEHICLE_PI_2
#define DIFFERENTIAL_CONTROL_BASE_WIDTH         0.71f
#define DIFFERENTIAL_INNER_MIN_RATIO_PERMILLE   300

static int32_t DifferentialControl_ClampPermille(int32_t value)
{
    if (value > DIFFERENTIAL_CONTROL_MAX_PERMILLE)
    {
        return DIFFERENTIAL_CONTROL_MAX_PERMILLE;
    }

    if (value < -DIFFERENTIAL_CONTROL_MAX_PERMILLE)
    {
        return -DIFFERENTIAL_CONTROL_MAX_PERMILLE;
    }

    return value;
}

static int32_t DifferentialControl_PermilleToPercent(int32_t value)
{
    value = DifferentialControl_ClampPermille(value);
    return (value * DIFFERENTIAL_CONTROL_PERCENT_SCALE) /
              DIFFERENTIAL_CONTROL_MAX_PERMILLE;
}

static DifferentialControl_Direction_t DifferentialControl_GetDirection(
    int32_t left_permille,
    int32_t right_permille)
{
    int32_t left;
    int32_t right;

    left = DifferentialControl_ClampPermille(left_permille);
    right = DifferentialControl_ClampPermille(right_permille);
    if ((left == 0) && (right == 0)){
        return DIFFERENTIAL_DIRECTION_STOP;
    }
    
    if (left == right)
    {
        return (left > 0) ? DIFFERENTIAL_DIRECTION_FORWARD :
               DIFFERENTIAL_DIRECTION_BACKWARD;
    }
    if (left == -right)
    {
        return (left < 0) ? DIFFERENTIAL_DIRECTION_ROTATE_LEFT :
               DIFFERENTIAL_DIRECTION_ROTATE_RIGHT;
    }
    return (right > left) ? DIFFERENTIAL_DIRECTION_TURN_LEFT :
           DIFFERENTIAL_DIRECTION_TURN_RIGHT;
}

void DifferentialControl_Analyze(int32_t left_permille,
                                 int32_t right_permille,
                                 DIFFERENTIAL_SPECS_t *specs)
{
    
    float left_speed_mps;
    float right_speed_mps;

    if (specs == NULL)
    {
        return;
    }

    left_permille = DifferentialControl_ClampPermille(left_permille);
    right_permille = DifferentialControl_ClampPermille(right_permille);

    left_speed_mps = ((float)left_permille / 1000.0f) * DIFFERENTIAL_CONTROL_SPEED_MAX;

    right_speed_mps = ((float)right_permille / 1000.0f) * DIFFERENTIAL_CONTROL_SPEED_MAX;

   
    specs->V_left = left_speed_mps;
    specs->V_right = right_speed_mps;
    specs->V = (left_speed_mps + right_speed_mps) / 2.0f;
    specs->omega = (right_speed_mps - left_speed_mps) / DIFFERENTIAL_CONTROL_BASE_WIDTH;

    /* Current vehicle mapping: M1/M2 are right, M3/M4 are left. */
    specs->V_FL = specs->V_left;
    specs->V_RL = specs->V_left;
    specs->V_FR = specs->V_right;
    specs->V_RR = specs->V_right;
    specs->direction = DifferentialControl_GetDirection(left_permille, right_permille);
}
static void DifferentialControl_LimitInnerWheel(int32_t *left_permille, int32_t *right_permille)
{
    int32_t left;
    int32_t right;
    int32_t sum;
    int32_t difference;
    int32_t max_difference;

    if ((left_permille == NULL) || (right_permille == NULL))
    {
        return;
    }

    left = *left_permille;
    right = *right_permille;

    /*
     * Hai bên ngược dấu là quay tại chỗ.
     * Giữ nguyên để vehicle_control xử lý pivot.
     */
    if (left == -right)
    {
        return;
    }

    if ((left == 0) && (right == 0))
    {
        return;
    }

    sum = left + right;
    difference = right - left;

    max_difference =
        (abs(sum) *
         (1000 - DIFFERENTIAL_INNER_MIN_RATIO_PERMILLE)) /
        (1000 + DIFFERENTIAL_INNER_MIN_RATIO_PERMILLE);

    if (difference > max_difference)
    {
        difference = max_difference;
    }
    else if (difference < -max_difference)
    {
        difference = -max_difference;
    }

    *left_permille = (sum - difference) / 2;
    *right_permille = (sum + difference) / 2;
}

void DifferentialControl_Compute(int32_t left_permille,
                                 int32_t right_permille,
                                 DifferentialControl_Command_t *command)
{
    int32_t left_percent;
    int32_t right_percent;

    if (command == NULL)
    {
        return;
    }
    left_permille = DifferentialControl_ClampPermille(left_permille);
    right_permille = DifferentialControl_ClampPermille(right_permille);
    DifferentialControl_LimitInnerWheel(&left_permille,
                                    &right_permille);

    left_percent = DifferentialControl_PermilleToPercent(left_permille);
    right_percent = DifferentialControl_PermilleToPercent(right_permille);

    /* Chassis mapping: M1/M2 are right, M3/M4 are left. */
    command->m1_percent = right_percent;
    command->m2_percent = right_percent;
    command->m3_percent = left_percent;
    command->m4_percent = left_percent;
    command->direction = DifferentialControl_GetDirection(left_permille, right_permille);
}
