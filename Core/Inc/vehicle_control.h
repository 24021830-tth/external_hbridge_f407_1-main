/*
 * Vehicle-level control.
 *
 * Chassis mapping:
 *   M1/M2 = right side
 *   M3/M4 = left side
 */
#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H
#include <stdint.h>
#include "app_control.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    AppControl_Snapshot_t app;
} VehicleControl_Input_t;

void VehicleControl_Init(void);
void VehicleControl_Stop(void);
void VehicleControl_Update(const VehicleControl_Input_t *input);

#ifdef __cplusplus
}
#endif

#endif /* VEHICLE_CONTROL_H */
