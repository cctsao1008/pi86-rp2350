#ifndef RP86_FREERTOS_CONFIG_H
#define RP86_FREERTOS_CONFIG_H

#define RP86_FREERTOS_CONFIG_INCLUDED           1
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 5000000UL )
#define configTICK_RATE_HZ                      100
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configMAX_PRIORITIES                    4
#define configMINIMAL_STACK_SIZE                96
#define configMAX_TASK_NAME_LEN                 8
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_16_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configQUEUE_REGISTRY_SIZE               0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configUSE_MINI_LIST_ITEM                1
#define configUSE_NEWLIB_REENTRANT              0

#define configUSE_TIMERS                        0
#define configUSE_EVENT_GROUPS                  0
#define configUSE_STREAM_BUFFERS                0

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   8192
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0
#define configHEAP_CLEAR_MEMORY_ON_FREE         0
#define configENABLE_HEAP_PROTECTOR             0

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskDelay                      0
#define INCLUDE_vTaskDelayUntil                 0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          0
#define INCLUDE_xTaskGetCurrentTaskHandle       0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskPrioritySet                0

void rp86AssertFailed( unsigned short line );
#define configASSERT( x ) do { if( ( x ) == 0 ) { rp86AssertFailed( ( unsigned short ) __LINE__ ); } } while( 0 )

#endif /* RP86_FREERTOS_CONFIG_H */
