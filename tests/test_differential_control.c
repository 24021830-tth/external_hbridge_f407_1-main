#include <stdio.h>
#include <stdlib.h>

#include "differential_control.h"

static void ExpectCommand(int32_t left_permille,
                          int32_t right_permille,
                          int32_t m1,
                          int32_t m2,
                          int32_t m3,
                          int32_t m4,
                          DifferentialControl_Direction_t direction)
{
    DifferentialControl_Command_t command = {0};

    DifferentialControl_Compute(left_permille, right_permille, &command);

    if ((command.m1_percent != m1) ||
        (command.m2_percent != m2) ||
        (command.m3_percent != m3) ||
        (command.m4_percent != m4) ||
        (command.direction != direction))
    {
        fprintf(stderr,
                "input=(%ld,%ld) actual=(%ld,%ld,%ld,%ld,%d)\n",
                (long)left_permille,
                (long)right_permille,
                (long)command.m1_percent,
                (long)command.m2_percent,
                (long)command.m3_percent,
                (long)command.m4_percent,
                (int)command.direction);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    ExpectCommand(0, 0, 0, 0, 0, 0,
                  DIFFERENTIAL_DIRECTION_STOP);
    ExpectCommand(1000, 1000, 100, 100, 100, 100,
                  DIFFERENTIAL_DIRECTION_FORWARD);
    ExpectCommand(-1000, -1000, -100, -100, -100, -100,
                  DIFFERENTIAL_DIRECTION_BACKWARD);
    ExpectCommand(300, 700, 70, 70, 30, 30,
                  DIFFERENTIAL_DIRECTION_TURN_LEFT);
    ExpectCommand(700, 300, 30, 30, 70, 70,
                  DIFFERENTIAL_DIRECTION_TURN_RIGHT);
    ExpectCommand(-1000, 1000, 100, 100, -100, -100,
                  DIFFERENTIAL_DIRECTION_ROTATE_LEFT);
    ExpectCommand(1000, -1000, -100, -100, 100, 100,
                  DIFFERENTIAL_DIRECTION_ROTATE_RIGHT);
    ExpectCommand(1500, -1500, -100, -100, 100, 100,
                  DIFFERENTIAL_DIRECTION_ROTATE_RIGHT);

    puts("differential_control: all tests passed");
    return EXIT_SUCCESS;
}
