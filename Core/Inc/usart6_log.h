
#ifndef USART6_LOG_H
#define USART6_LOG_H
#include <stdint.h>
#include "dc3w2_protocol.h"
#include "current.h"
#ifdef __cplusplus
extern "C" {
#endif
void Usart6Log_Init(void);
void Usart6Log_Process(void);
void Usart6Log_IrqHandler(void);
void Usart6Log_Write(const char *text);
void Usart6Log_CurrentSnapshot(const CurrentSense_Snapshot_t *snapshot);
                           
#ifdef __cplusplus
}
#endif

#endif /* USART6_LOG_H */
