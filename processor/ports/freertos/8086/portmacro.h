#ifndef RP86_FREERTOS_PORTMACRO_H
#define RP86_FREERTOS_PORTMACRO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       int
#define portSTACK_TYPE  uint16_t
#define portBASE_TYPE   short
#define portPOINTER_SIZE_TYPE uint16_t

typedef portSTACK_TYPE StackType_t;
typedef short BaseType_t;
typedef unsigned short UBaseType_t;

#if ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS )
    typedef uint16_t TickType_t;
    #define portMAX_DELAY ( ( TickType_t ) 0xffffU )
    #define portTICK_TYPE_IS_ATOMIC 1
#else
    #error RP86 FreeRTOS/8086 v1 requires a 16-bit TickType_t.
#endif

/*
 * Open Watcom treats these #pragma aux definitions as inline instruction
 * sequences.  PUSHF/POPF preserves the caller's interrupt state and naturally
 * nests on the current task stack, matching the historical 16-bit x86 ports.
 */
void portLOCAL_ENTER_CRITICAL( void );
#pragma aux portLOCAL_ENTER_CRITICAL = "pushf" "cli";
#define portENTER_CRITICAL() portLOCAL_ENTER_CRITICAL()

void portEXIT_CRITICAL( void );
#pragma aux portEXIT_CRITICAL = "popf";

void portDISABLE_INTERRUPTS( void );
#pragma aux portDISABLE_INTERRUPTS = "cli";

void portENABLE_INTERRUPTS( void );
#pragma aux portENABLE_INTERRUPTS = "sti";

#define portSTACK_GROWTH        ( -1 )
#define portBYTE_ALIGNMENT      2
#define portINITIAL_SW          ( ( StackType_t ) 0x0202U )
#define portTICK_PERIOD_MS      ( ( TickType_t ) ( 1000U / configTICK_RATE_HZ ) )
#define portYIELD_INTERRUPT     0x80

/* Vector 80h is workload-owned software yield; vector 21h is the RP86 tick. */
#define portYIELD()             __asm { int portYIELD_INTERRUPT }
#define portYIELD_WITHIN_API()  portYIELD()
#define portNOP()               __asm { nop }

#define portTASK_FUNCTION_PROTO( vTaskFunction, pvParameters ) void vTaskFunction( void * pvParameters )
#define portTASK_FUNCTION( vTaskFunction, pvParameters )       void vTaskFunction( void * pvParameters )

#ifdef __cplusplus
}
#endif

#endif /* RP86_FREERTOS_PORTMACRO_H */
