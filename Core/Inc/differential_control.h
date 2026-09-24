/*
 * Differential-drive command conversion.
 *
 * The application sends independent left/right side commands in permille:
 *
 *     -1000 .. 0 .. +1000
 *
 * These commands already represent the requested speed of each side.  This
 * module converts them to four explicit motor commands and also exposes the
 * normalized chassis values V/omega for direction analysis.
 */
#ifndef DIFFERENTIAL_CONTROL_H
#define DIFFERENTIAL_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DIFFERENTIAL_DIRECTION_STOP = 0,
    DIFFERENTIAL_DIRECTION_FORWARD,
    DIFFERENTIAL_DIRECTION_BACKWARD,
    DIFFERENTIAL_DIRECTION_TURN_LEFT,
    DIFFERENTIAL_DIRECTION_TURN_RIGHT,
    DIFFERENTIAL_DIRECTION_ROTATE_LEFT,
    DIFFERENTIAL_DIRECTION_ROTATE_RIGHT
} DifferentialControl_Direction_t;

typedef struct
{
    int32_t m1_percent;  /* Right-front side command. */
    int32_t m2_percent;  /* Right-rear side command. */
    int32_t m3_percent;  /* Left-front side command. */
    int32_t m4_percent;  /* Left-rear side command. */
    DifferentialControl_Direction_t direction;
} DifferentialControl_Command_t;

typedef struct
{
    float V_left;       // m/s
    float V_right;      // m/s
    float V;            // m/s
    float omega;        // rad/s

    float V_FL;         // m/s
    float V_RL;         // m/s
    float V_FR;         // m/s
    float V_RR;         // m/s

    DifferentialControl_Direction_t direction;
} DIFFERENTIAL_SPECS_t;

void DifferentialControl_Compute(int32_t left_permille,
                                 int32_t right_permille,
                                 DifferentialControl_Command_t *command);

void DifferentialControl_Analyze(int32_t left_permille,
                                 int32_t right_permille,
                                 DIFFERENTIAL_SPECS_t *specs);

#ifdef __cplusplus
}
#endif

#endif /* DIFFERENTIAL_CONTROL_H */
