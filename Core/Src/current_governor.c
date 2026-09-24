#include "current_governor.h"

#include <stddef.h>
#include <string.h>

static uint16_t CurrentGovernor_Slew(uint16_t current,
                                     uint16_t target,
                                     uint32_t rate_permille_s,
                                     uint32_t dt_ms)
{
    uint32_t step;

    if (current == target || dt_ms == 0U)
    {
        return current;
    }

    step = (rate_permille_s * dt_ms + 999U) / 1000U;
    if (step == 0U)
    {
        step = 1U;
    }

    if (target < current)
    {
        return ((uint32_t)(current - target) <= step) ?
               target : (uint16_t)(current - step);
    }

    return ((uint32_t)(target - current) <= step) ?
           target : (uint16_t)(current + step);
}

void CurrentGovernor_Init(CurrentGovernor_State_t *state)
{
    if (state == NULL)
    {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->common_scale_permille = CURRENT_GOVERNOR_SCALE_PERMILLE;
}

void CurrentGovernor_Update(CurrentGovernor_State_t *state,
                            const CurrentSense_ControlFrame_t *frame,
                            uint8_t required_mask,
                            uint32_t now_ms,
                            uint32_t dt_ms,
                            CurrentGovernor_Result_t *result)
{
    uint16_t target_scale = CURRENT_GOVERNOR_SCALE_PERMILLE;
    uint8_t usable_mask;
    uint8_t new_snapshot;
    uint32_t motor;

    if (state == NULL || result == NULL)
    {
        return;
    }

    memset(result, 0, sizeof(*result));

    if (frame == NULL || frame->governor_enabled == 0U)
    {
        CurrentGovernor_Init(state);
        result->common_scale_permille = CURRENT_GOVERNOR_SCALE_PERMILLE;
        return;
    }

    result->enabled = 1U;
    usable_mask = frame->snapshot.valid_mask;
    if (Current_IsSnapshotFresh(&frame->snapshot,
                                now_ms,
                                CURRENT_SNAPSHOT_TIMEOUT_MS) == 0U)
    {
        usable_mask = 0U;
    }

    if ((required_mask & usable_mask) != required_mask)
    {
        if (state->invalid_duration_ms < (UINT32_MAX - dt_ms))
        {
            state->invalid_duration_ms += dt_ms;
        }
        else
        {
            state->invalid_duration_ms = UINT32_MAX;
        }

        if (state->invalid_duration_ms >=
            CURRENT_GOVERNOR_INVALID_CONFIRM_MS)
        {
            state->data_fault_latched = 1U;
        }
    }
    else
    {
        state->invalid_duration_ms = 0U;
    }

    new_snapshot = (frame->snapshot.generation != state->last_generation) ?
                   1U : 0U;
    if (new_snapshot != 0U)
    {
        state->last_generation = frame->snapshot.generation;
    }

    for (motor = 0U; motor < CURRENT_MOTOR_COUNT; ++motor)
    {
        uint8_t bit = (uint8_t)(1U << motor);
        const CurrentSense_ProtectionLimits_t *limits =
            &frame->limits[motor];
        const CurrentSense_MotorSample_t *sample =
            &frame->snapshot.motor[motor];

        if ((required_mask & bit) == 0U ||
            (usable_mask & bit) == 0U)
        {
            continue;
        }

        if (limits->soft_limit_ma > 0U &&
            sample->current_control_ma > (int32_t)limits->soft_limit_ma)
        {
            uint32_t scale = (uint32_t)(
                ((uint64_t)limits->soft_limit_ma *
                 CURRENT_GOVERNOR_SCALE_PERMILLE) /
                (uint32_t)sample->current_control_ma);

            result->soft_limit_mask |= bit;
            if (scale < target_scale)
            {
                target_scale = (uint16_t)scale;
            }
        }

        if (new_snapshot != 0U && limits->hard_limit_ma > 0U)
        {
            if (sample->current_fast_ma >=
                (int32_t)limits->hard_limit_ma)
            {
                if (state->hard_confirm_count[motor] < UINT16_MAX)
                {
                    state->hard_confirm_count[motor]++;
                }
            }
            else
            {
                state->hard_confirm_count[motor] = 0U;
            }

            if (state->hard_confirm_count[motor] >=
                CURRENT_GOVERNOR_HARD_CONFIRM_SAMPLES)
            {
                state->hard_fault_latched_mask |= bit;
            }
        }
    }

    if (state->data_fault_latched != 0U)
    {
        target_scale = 0U;
        result->controlled_stop = 1U;
    }

    if (state->hard_fault_latched_mask != 0U)
    {
        state->common_scale_permille = 0U;
    }
    else if (target_scale < state->common_scale_permille)
    {
        state->common_scale_permille = CurrentGovernor_Slew(
            state->common_scale_permille,
            target_scale,
            CURRENT_GOVERNOR_ATTACK_PERMILLE_S,
            dt_ms);
    }
    else
    {
        state->common_scale_permille = CurrentGovernor_Slew(
            state->common_scale_permille,
            target_scale,
            CURRENT_GOVERNOR_RELEASE_PERMILLE_S,
            dt_ms);
    }

    result->common_scale_permille = state->common_scale_permille;
    result->hard_fault_mask = state->hard_fault_latched_mask;
}

int32_t CurrentGovernor_ApplyScale(int32_t command,
                                  uint16_t scale_permille)
{
    uint32_t magnitude;
    uint32_t scaled;

    if (scale_permille > CURRENT_GOVERNOR_SCALE_PERMILLE)
    {
        scale_permille = CURRENT_GOVERNOR_SCALE_PERMILLE;
    }

    if (command == 0)
    {
        return 0;
    }

    magnitude = (command < 0) ?
                (uint32_t)(-(int64_t)command) : (uint32_t)command;
    scaled = (magnitude * scale_permille) /
             CURRENT_GOVERNOR_SCALE_PERMILLE;

    return (command < 0) ? -(int32_t)scaled : (int32_t)scaled;
}
