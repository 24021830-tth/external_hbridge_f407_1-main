#ifndef INC_CURRENT_H_
#define INC_CURRENT_H_

#include <stdint.h>
#define CURRENT_MOTOR_COUNT      4U

#define CURRENT_SNAPSHOT_TIMEOUT_MS          100U
#define CURRENT_DEFAULT_SOFT_LIMIT_MA     12000UL
#define CURRENT_DEFAULT_HARD_LIMIT_MA     18000UL
#define CURRENT_HARD_LIMIT_CONFIRM_BLOCKS      2U

typedef enum
{
    CURRENT_MOTOR_1 = 0,
    CURRENT_MOTOR_2,
    CURRENT_MOTOR_3,
    CURRENT_MOTOR_4

} CurrentMotor_t;


typedef struct
{
    uint16_t offset_adc;
    uint16_t gain_permille;

} CurrentSense_Calibration_t;

typedef struct
{
    uint32_t soft_limit_ma;
    uint32_t hard_limit_ma;
} CurrentSense_ProtectionLimits_t;

typedef struct
{
    uint16_t raw_adc[CURRENT_MOTOR_COUNT];
    uint16_t average_adc[CURRENT_MOTOR_COUNT];
    uint16_t peak_adc[CURRENT_MOTOR_COUNT];

    int32_t current_ma[CURRENT_MOTOR_COUNT];
    int32_t filtered_ma[CURRENT_MOTOR_COUNT];
    int32_t peak_ma[CURRENT_MOTOR_COUNT];
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint8_t valid_mask;
    uint8_t calibrated;
    uint8_t soft_limit_mask;
    uint8_t hard_fault_mask;

} CurrentSense_Snapshot_t;

void Current_Init(void);
void Current_Stop(void);
void Current_Task(void *argument);
void Current_Start(void);
void Current_DMA_IRQHandler(void);
void Current_GetSnapshot(CurrentSense_Snapshot_t *snapshot);

void Current_SetCalibration(CurrentMotor_t motor,
                            uint16_t offset_adc,
                            uint16_t gain_permille);

void Current_GetCalibration(CurrentMotor_t motor,
                            CurrentSense_Calibration_t *calibration);

void Current_SetProtectionLimits(CurrentMotor_t motor,
                                 uint32_t soft_limit_ma,
                                 uint32_t hard_limit_ma);

void Current_GetProtectionLimits(CurrentMotor_t motor,
                                 CurrentSense_ProtectionLimits_t *limits);

uint8_t Current_IsSnapshotFresh(const CurrentSense_Snapshot_t *snapshot,
                                uint32_t timeout_ms);
#endif /* INC_CURRENT_H_ */
