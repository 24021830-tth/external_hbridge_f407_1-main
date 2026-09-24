#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "current_governor.h"

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

static void Fail(const char *message)
{
    fprintf(stderr, "current_governor: %s\n", message);
    exit(EXIT_FAILURE);
}

static void InitFrame(CurrentSense_ControlFrame_t *frame)
{
    uint32_t motor;

    memset(frame, 0, sizeof(*frame));
    frame->governor_enabled = 1U;
    frame->snapshot.valid_mask = CURRENT_VALID_ALL;
    frame->snapshot.generation = 1U;
    frame->snapshot.timestamp_ms = 100U;

    for (motor = 0U; motor < CURRENT_MOTOR_COUNT; ++motor)
    {
        frame->limits[motor].soft_limit_ma = 10000U;
        frame->limits[motor].hard_limit_ma = 20000U;
        frame->snapshot.motor[motor].current_control_ma = 5000;
        frame->snapshot.motor[motor].current_fast_ma = 5000;
    }
}

static void TestCommonReductionPreservesSigns(void)
{
    CurrentGovernor_State_t state;
    CurrentGovernor_Result_t result;
    CurrentSense_ControlFrame_t frame;
    int32_t right;
    int32_t left;

    InitFrame(&frame);
    CurrentGovernor_Init(&state);
    frame.snapshot.motor[0].current_control_ma = 15000;

    CurrentGovernor_Update(&state, &frame, CURRENT_VALID_ALL,
                           100U, 100U, &result);

    if (result.common_scale_permille != 666U ||
        result.soft_limit_mask != 0x01U)
    {
        Fail("soft limit did not choose the strictest motor scale");
    }

    right = CurrentGovernor_ApplyScale(800, result.common_scale_permille);
    left = CurrentGovernor_ApplyScale(-400, result.common_scale_permille);
    if (right != 532 || left != -266 || right > 800 || left < -400)
    {
        Fail("common scale changed sign or boosted a command");
    }
}

static void TestHardFaultNeedsNewSamples(void)
{
    CurrentGovernor_State_t state;
    CurrentGovernor_Result_t result;
    CurrentSense_ControlFrame_t frame;

    InitFrame(&frame);
    CurrentGovernor_Init(&state);
    frame.snapshot.motor[2].current_fast_ma = 25000;

    CurrentGovernor_Update(&state, &frame, CURRENT_VALID_ALL,
                           100U, 1U, &result);
    CurrentGovernor_Update(&state, &frame, CURRENT_VALID_ALL,
                           101U, 1U, &result);
    if (result.hard_fault_mask != 0U)
    {
        Fail("same DMA snapshot was counted twice for hard fault");
    }

    frame.snapshot.generation++;
    frame.snapshot.timestamp_ms++;
    CurrentGovernor_Update(&state, &frame, CURRENT_VALID_ALL,
                           101U, 1U, &result);
    if (result.hard_fault_mask != 0x04U ||
        result.common_scale_permille != 0U)
    {
        Fail("hard overcurrent was not latched after two samples");
    }
}

static void TestInvalidDataControlledStop(void)
{
    CurrentGovernor_State_t state;
    CurrentGovernor_Result_t result;
    CurrentSense_ControlFrame_t frame;

    InitFrame(&frame);
    CurrentGovernor_Init(&state);
    frame.snapshot.valid_mask = 0U;

    CurrentGovernor_Update(&state, &frame, CURRENT_VALID_ALL,
                           100U, 25U, &result);
    CurrentGovernor_Update(&state, &frame, CURRENT_VALID_ALL,
                           125U, 25U, &result);

    if (result.controlled_stop == 0U ||
        result.common_scale_permille >= CURRENT_GOVERNOR_SCALE_PERMILLE)
    {
        Fail("invalid current data did not start a controlled stop");
    }
}

int main(void)
{
    TestCommonReductionPreservesSigns();
    TestHardFaultNeedsNewSamples();
    TestInvalidDataControlledStop();

    puts("current_governor: all tests passed");
    return EXIT_SUCCESS;
}
