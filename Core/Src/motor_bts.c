#include "motor_bts.h"

#include "main.h"
#include "stm32f407xx.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_tim.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Pin/timer mapping from external_hbridge_f407.ioc:
 *
 * M1: TIM1_CH1/CH2 (PE9/PE11), INH A/B (PE10/PE12)
 * M2: TIM1_CH3/CH4 (PE13/PE14), INH A/B (PB10/PB12)
 * M3: TIM3_CH1/CH2 (PA6/PA7), INH A/B (PC4/PC5)
 * M4: TIM2_CH3/CH4 (PA2/PA3), INH A/B (PB0/PB1)
 *
 * The pin macros are generated in main.h so this module follows the current
 * CubeMX configuration instead of duplicating GPIO port addresses.
 */
typedef struct
{
    TIM_TypeDef *timer;
    uint32_t pwm_a;
    uint32_t pwm_b;
    GPIO_TypeDef *inh_port;
    uint32_t inh_pins;
} MotorBts_Config_t;

static const MotorBts_Config_t motor_config[MOTOR_BTS_MOTOR_COUNT] =
{
    {
        TIM1,
        LL_TIM_CHANNEL_CH1,
        LL_TIM_CHANNEL_CH2,
        M1_INH_A_GPIO_Port,
        M1_INH_A_Pin | M1_INH_B_Pin
    },
    {
        TIM1,
        LL_TIM_CHANNEL_CH3,
        LL_TIM_CHANNEL_CH4,
        M2_INH_A_GPIO_Port,
        M2_INH_A_Pin | M2_INH_B_Pin
    },
    {
        TIM3,
        LL_TIM_CHANNEL_CH1,
        LL_TIM_CHANNEL_CH2,
        M3_INH_A_GPIO_Port,
        M3_INH_A_Pin | M3_INH_B_Pin
    },
    {
        TIM2,
        LL_TIM_CHANNEL_CH3,
        LL_TIM_CHANNEL_CH4,
        M4_INH_A_GPIO_Port,
        M4_INH_A_Pin | M4_INH_B_Pin
    }
};

static MOTOR_DIR_t motor_direction[MOTOR_BTS_MOTOR_COUNT] =
{
    MOTOR_BTS_A,
    MOTOR_BTS_A,
    MOTOR_BTS_A,
    MOTOR_BTS_A
};

typedef struct
{
    volatile uint32_t guard;
    volatile int32_t command_permille;
    volatile MOTOR_DIR_t direction;
    volatile MotorBts_OutputMode_t mode;
    volatile uint32_t transition_generation;
} MotorBts_AppliedStateStorage_t;

static MotorBts_AppliedStateStorage_t
    motor_applied_state[MOTOR_BTS_MOTOR_COUNT];

static void MotorBts_PublishAppliedState(MOTOR_INDEX_t motor,
                                         MotorBts_OutputMode_t mode,
                                         MOTOR_DIR_t direction,
                                         int32_t command_permille)
{
    MotorBts_AppliedStateStorage_t *state;

    if ((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT)
    {
        return;
    }

    state = &motor_applied_state[motor];
    state->guard++;
    __DMB();

    if (state->mode != mode || state->direction != direction)
    {
        state->transition_generation++;
    }
    state->mode = mode;
    state->direction = direction;
    state->command_permille = command_permille;

    __DMB();
    state->guard++;
}

static uint32_t MotorBts_PercentToCompare(TIM_TypeDef *timer,
                                          uint32_t percent)
{
    const uint32_t period = LL_TIM_GetAutoReload(timer) + 1U;

    if (percent > 100U)
    {
        percent = 100U;
    }

    return (percent * period) / 100U;
}

static void MotorBts_SetCompare(TIM_TypeDef *timer,
                                uint32_t channel,
                                uint32_t compare)
{
    switch (channel)
    {
        case LL_TIM_CHANNEL_CH1:
            LL_TIM_OC_SetCompareCH1(timer, compare);
            break;
        case LL_TIM_CHANNEL_CH2:
            LL_TIM_OC_SetCompareCH2(timer, compare);
            break;
        case LL_TIM_CHANNEL_CH3:
            LL_TIM_OC_SetCompareCH3(timer, compare);
            break;
        case LL_TIM_CHANNEL_CH4:
            LL_TIM_OC_SetCompareCH4(timer, compare);
            break;
        default:
            break;
    }
}

static void MotorBts_StopOneInternal(MOTOR_INDEX_t motor)
{
    const MotorBts_Config_t *config;

    if ((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT)
    {
        return;
    }

    config = &motor_config[motor];
    /* Disable the bridge before changing the PWM compare values. */
    LL_GPIO_ResetOutputPin(config->inh_port, config->inh_pins);
    MotorBts_Stop(config->timer, config->pwm_a | config->pwm_b);

    /*
     * Keep the timer channels connected while stopped.  With PWM1 and CCR=0
     * this actively drives the PWM inputs low; disabling CCxE would leave an
     * AF pin undriven and allow the BTS input to float.
     */
    LL_TIM_CC_EnableChannel(config->timer, config->pwm_a | config->pwm_b);
    MotorBts_PublishAppliedState(motor,
                                 MOTOR_BTS_OUTPUT_COAST,
                                 motor_direction[motor],
                                 0);
}

static void MotorBts_SetSpeedOneInternal(MOTOR_INDEX_t motor,
                                         uint32_t percent)
{
    const MotorBts_Config_t *config;
    uint32_t compare;
    uint32_t active_channel;
    uint32_t inactive_channel;

    if ((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT)
    {
        return;
    }

    config = &motor_config[motor];

    if (percent == 0U)
    {
        MotorBts_StopOneInternal(motor);
        return;
    }

    compare = MotorBts_PercentToCompare(config->timer, percent);

    if (motor_direction[motor] == MOTOR_BTS_A)
    {
        active_channel = config->pwm_a;
        inactive_channel = config->pwm_b;
    }
    else
    {
        active_channel = config->pwm_b;
        inactive_channel = config->pwm_a;
    }

    /* Never allow both BTS PWM inputs to be driven simultaneously. */
    MotorBts_SetCompare(config->timer, inactive_channel, 0U);
    MotorBts_SetCompare(config->timer, active_channel, compare);
    LL_TIM_CC_EnableChannel(config->timer, config->pwm_a | config->pwm_b);

    /* Enable the bridge only after both PWM inputs have been prepared. */
    LL_GPIO_SetOutputPin(config->inh_port, config->inh_pins);
    MotorBts_PublishAppliedState(
        motor,
        MOTOR_BTS_OUTPUT_DRIVE,
        motor_direction[motor],
        (motor_direction[motor] == MOTOR_BTS_A) ?
        (int32_t)(percent * 10U) : -(int32_t)(percent * 10U));
}

void MotorBts_Stop(TIM_TypeDef *timer, uint32_t channels)
{
    if ((channels & LL_TIM_CHANNEL_CH1) != 0U)
    {
        LL_TIM_OC_SetCompareCH1(timer, 0U);
    }
    if ((channels & LL_TIM_CHANNEL_CH2) != 0U)
    {
        LL_TIM_OC_SetCompareCH2(timer, 0U);
    }
    if ((channels & LL_TIM_CHANNEL_CH3) != 0U)
    {
        LL_TIM_OC_SetCompareCH3(timer, 0U);
    }
    if ((channels & LL_TIM_CHANNEL_CH4) != 0U)
    {
        LL_TIM_OC_SetCompareCH4(timer, 0U);
    }
}

void MotorBts_InitSafe(void)
{
    uint32_t index;
    LL_GPIO_InitTypeDef gpio_init = {0};
    const uint32_t tim1_channels = LL_TIM_CHANNEL_CH1 |
                                   LL_TIM_CHANNEL_CH2 |
                                   LL_TIM_CHANNEL_CH3 |
                                   LL_TIM_CHANNEL_CH4;
    const uint32_t tim2_channels = LL_TIM_CHANNEL_CH3 |
                                   LL_TIM_CHANNEL_CH4;

    const uint32_t tim3_channels = LL_TIM_CHANNEL_CH1 |
                                   LL_TIM_CHANNEL_CH2;

    /* Keep the bridge disabled while compare values and output channels settle. */
    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        LL_GPIO_ResetOutputPin(motor_config[index].inh_port,
                               motor_config[index].inh_pins);

        gpio_init.Pin = motor_config[index].inh_pins;
        gpio_init.Mode = LL_GPIO_MODE_OUTPUT;
        gpio_init.Speed = LL_GPIO_SPEED_FREQ_LOW;
        gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
        gpio_init.Pull = LL_GPIO_PULL_NO;
        LL_GPIO_Init(motor_config[index].inh_port, &gpio_init);

        MotorBts_StopOneInternal((MOTOR_INDEX_t)index);
    }

    LL_TIM_CC_DisableChannel(TIM1, tim1_channels);
    LL_TIM_CC_DisableChannel(TIM2, tim2_channels);
    LL_TIM_CC_DisableChannel(TIM3, tim3_channels);
    LL_TIM_DisableAllOutputs(TIM1);

    /* TIM1 is advanced and needs MOE; TIM2/TIM3 need no BDTR/MOE. */
    LL_TIM_EnableAllOutputs(TIM1);
    LL_TIM_EnableCounter(TIM1);
    LL_TIM_EnableCounter(TIM2);
    LL_TIM_EnableCounter(TIM3);
    LL_TIM_CC_EnableChannel(TIM1, tim1_channels);
    LL_TIM_CC_EnableChannel(TIM2, tim2_channels);
    LL_TIM_CC_EnableChannel(TIM3, tim3_channels);

    /*
     * Leave every bridge disabled until a non-zero control command arrives.
     * The enabled timer channels and CCR=0 keep the PWM pins actively low.
     */
}

void MotorBts_SetSpeed(uint32_t percent)
{
    uint32_t index;

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        MotorBts_SetSpeedOneInternal((MOTOR_INDEX_t)index, percent);
    }
}

void MotorBts_StopOne(MOTOR_INDEX_t motor)
{
    MotorBts_StopOneInternal(motor);
}

void MotorBts_BrakeOne(MOTOR_INDEX_t motor){
    const MotorBts_Config_t *config;
    if ((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT){
        return;
    }
    config = &motor_config[motor];
     /*
     * BTS7960:
     * - INH = 1: cho phép cầu H hoạt động.
     * - IN = 0: bật nhánh low-side.
     * Muc tieu la bat low-side
     * Đặt cả hai PWM input về 0 và giữ INH ở mức 1
     * sẽ bật low-side của hai nửa cầu.
     * Hai đầu motor bị kéo về mass và motor được phanh động năng.
     */
    MotorBts_SetCompare(config->timer, config->pwm_a, 0U);
    MotorBts_SetCompare(config->timer, config->pwm_b, 0U);
    LL_TIM_CC_EnableChannel(config->timer,
                             config->pwm_a | config->pwm_b);
    LL_GPIO_SetOutputPin(config->inh_port, config->inh_pins);
    MotorBts_PublishAppliedState(motor,
                                 MOTOR_BTS_OUTPUT_BRAKE,
                                 motor_direction[motor],
                                 0);

}

void MotorBts_RunOne(MOTOR_INDEX_t motor,
                     MOTOR_DIR_t direction,
                     uint32_t percent)
{
    if ((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT)
    {
        return;
    }

    if ((direction != MOTOR_BTS_A) && (direction != MOTOR_BTS_B))
    {
        MotorBts_StopOne(motor);
        return;
    }

    if (motor_direction[motor] != direction)
    {
        /* Require a subsequent call before applying the new direction. */
        MotorBts_StopOneInternal(motor);
        motor_direction[motor] = direction;
        return;
    }

    MotorBts_SetSpeedOneInternal(motor, percent);
}

void MotorBts_RunCommand(MOTOR_INDEX_t motor, int32_t command_permille)
{
    if (command_permille > 1000)
    {
        command_permille = 1000;
    }
    else if (command_permille < -1000)
    {
        command_permille = -1000;
    }

    if (command_permille > 0)
    {
        MotorBts_RunOne(motor,
                        MOTOR_BTS_A,
                        (uint32_t)command_permille / 10U);
    }
    else if (command_permille < 0)
    {
        MotorBts_RunOne(motor,
                        MOTOR_BTS_B,
                        (uint32_t)(-command_permille) / 10U);
    }
    else
    {
        MotorBts_StopOne(motor);
    }
}

void MotorBts_DIR(MOTOR_DIR_t direction)
{
    uint32_t index;

    if ((direction != MOTOR_BTS_A) && (direction != MOTOR_BTS_B))
    {
        for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
        {
            MotorBts_StopOneInternal((MOTOR_INDEX_t)index);
        }
        return;
    }

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        if (motor_direction[index] != direction)
        {
            MotorBts_StopOneInternal((MOTOR_INDEX_t)index);
            motor_direction[index] = direction;
        }
    }
}

void MotorBts_Run(MOTOR_DIR_t direction, uint32_t percent)
{
    uint32_t index;
    uint32_t direction_changed = 0U;

    if ((direction != MOTOR_BTS_A) && (direction != MOTOR_BTS_B))
    {
        MotorBts_DIR(direction);
        return;
    }

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        if (motor_direction[index] != direction)
        {
            direction_changed = 1U;
            break;
        }
    }

    MotorBts_DIR(direction);

    if (direction_changed != 0U)
    {
        /* A full stop interval separates opposite directions. */
        return;
    }

    MotorBts_SetSpeed(percent);
}

void MotorBts_GetAppliedState(MOTOR_INDEX_t motor,
                              MotorBts_AppliedState_t *state)
{
    const MotorBts_AppliedStateStorage_t *source;
    uint32_t before;
    uint32_t after;

    if ((uint32_t)motor >= MOTOR_BTS_MOTOR_COUNT || state == NULL)
    {
        return;
    }

    source = &motor_applied_state[motor];
    do
    {
        before = source->guard;
        __DMB();
        state->command_permille = source->command_permille;
        state->direction = source->direction;
        state->mode = source->mode;
        state->transition_generation = source->transition_generation;
        __DMB();
        after = source->guard;
    }
    while (before != after || (before & 1U) != 0U);
}

void MotorBts_EmergencyDisableAll(void)
{
    uint32_t index;

    for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
    {
        const MotorBts_Config_t *config = &motor_config[index];

        LL_GPIO_ResetOutputPin(config->inh_port, config->inh_pins);
        MotorBts_SetCompare(config->timer, config->pwm_a, 0U);
        MotorBts_SetCompare(config->timer, config->pwm_b, 0U);
        MotorBts_PublishAppliedState((MOTOR_INDEX_t)index,
                                     MOTOR_BTS_OUTPUT_COAST,
                                     motor_direction[index],
                                     0);
    }
}
