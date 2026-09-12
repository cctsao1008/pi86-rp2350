#ifndef RP86_FREERTOS_SYSTEM_CONFIG_H
#define RP86_FREERTOS_SYSTEM_CONFIG_H

#define RP86_FREERTOS_CONFIG_INCLUDED           1
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 5000000UL )
#define configTICK_RATE_HZ                      100
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configTICKLESS_IDLE                     0
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
#define configLIST_VOLATILE                     volatile
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

#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          0
#define INCLUDE_xTaskGetCurrentTaskHandle       0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskPrioritySet                0

/*
 * Temporary Issue #71 queue-blocking localization hooks.  These use the
 * kernel's existing trace points instead of carrying a patched FreeRTOS kernel.
 * The workload arms the witness immediately before its first xQueueReceive().
 */
void rp86QueueTraceStage( unsigned short stage, unsigned short detail );
#define traceENTER_xQueueReceive( xQueue, pvBuffer, xTicksToWait ) \
    rp86QueueTraceStage( 2U, ( unsigned short ) ( xTicksToWait ) )
#define traceENTER_vTaskSuspendAll() \
    rp86QueueTraceStage( 3U, 0U )
#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue ) \
    rp86QueueTraceStage( 4U, 0U )
#define traceENTER_xTaskResumeAll() \
    rp86QueueTraceStage( 5U, 0U )
#define traceRETURN_xTaskResumeAll( xAlreadyYielded ) \
    rp86QueueTraceStage( 6U, ( unsigned short ) ( xAlreadyYielded ) )
#define traceRETURN_xQueueReceive( xReturn ) \
    rp86QueueTraceStage( 7U, ( unsigned short ) ( xReturn ) )
#define traceENTER_vTaskPlaceOnEventList( pxEventList, xTicksToWait ) \
    rp86QueueTraceStage( 10U, ( unsigned short ) ( xTicksToWait ) )
#define traceRETURN_vTaskPlaceOnEventList() \
    rp86QueueTraceStage( 11U, 0U )
#define traceENTER_vListInsert( pxList, pxNewListItem ) \
    rp86QueueTraceStage( 12U, 0U )
#define traceRETURN_vListInsert() \
    rp86QueueTraceStage( 13U, 0U )
#define traceENTER_vListInsertEnd( pxList, pxNewListItem ) \
    rp86QueueTraceStage( 14U, 0U )
#define traceRETURN_vListInsertEnd() \
    rp86QueueTraceStage( 15U, 0U )
#define traceENTER_uxListRemove( pxItemToRemove ) \
    rp86QueueTraceStage( 16U, 0U )
#define traceRETURN_uxListRemove( uxNumberOfItems ) \
    rp86QueueTraceStage( 17U, ( unsigned short ) ( uxNumberOfItems ) )

void rp86AssertFailed( unsigned short line );
#define configASSERT( x ) do { if( ( x ) == 0 ) { rp86AssertFailed( ( unsigned short ) __LINE__ ); } } while( 0 )

#endif /* RP86_FREERTOS_SYSTEM_CONFIG_H */
