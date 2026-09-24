#ifndef INC_CURRENT_GOVERNOR_H_
#define INC_CURRENT_GOVERNOR_H_

#include <stdint.h>

#include "current.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CURRENT_GOVERNOR_SCALE_PERMILLE        1000U
#define CURRENT_GOVERNOR_HARD_CONFIRM_SAMPLES     2U
#define CURRENT_GOVERNOR_INVALID_CONFIRM_MS       50U
#define CURRENT_GOVERNOR_ATTACK_PERMILLE_S      5000U
#define CURRENT_GOVERNOR_RELEASE_PERMILLE_S      250U

typedef struct
{
    uint16_t common_scale_permille;
    uint16_t hard_confirm_count[CURRENT_MOTOR_COUNT];
    uint32_t last_generation;
    uint32_t invalid_duration_ms;
    uint8_t data_fault_latched;
    uint8_t hard_fault_latched_mask;
} CurrentGovernor_State_t;

typedef struct
{
    uint16_t common_scale_permille;
    uint8_t soft_limit_mask;
    uint8_t hard_fault_mask;
    uint8_t controlled_stop;
    uint8_t enabled;
} CurrentGovernor_Result_t;

void CurrentGovernor_Init(CurrentGovernor_State_t *state);

/*
 * required_mask contains only motors whose applied duty is currently high
 * enough for the BTS IS signal to be observable.
 */
void CurrentGovernor_Update(CurrentGovernor_State_t *state,
                            const CurrentSense_ControlFrame_t *frame,
                            uint8_t required_mask,
                            uint32_t now_ms,
                            uint32_t dt_ms,
                            CurrentGovernor_Result_t *result);

/* Apply one common, reduction-only scale without changing command sign. */
int32_t CurrentGovernor_ApplyScale(int32_t command,
                                  uint16_t scale_permille);

#ifdef __cplusplus
}
#endif

#endif /* INC_CURRENT_GOVERNOR_H_ */
