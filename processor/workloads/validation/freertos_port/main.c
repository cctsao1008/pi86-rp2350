#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define RP86_PASS_RESULT          0x6060U
#define RP86_FAIL_CREATE_SEM      0x60A1U
#define RP86_FAIL_CREATE_QUEUE    0x60A2U
#define RP86_FAIL_CREATE_TASK_A   0x60A3U
#define RP86_FAIL_CREATE_TASK_B   0x60A4U
#define RP86_FAIL_SEM_TAKE        0x60B1U
#define RP86_FAIL_SEM_GIVE        0x60B2U
#define RP86_FAIL_QUEUE_RECV      0x60B3U
#define RP86_FAIL_QUEUE_SEND      0x60B4U
#define RP86_FAIL_QUEUE_VALUE     0x60B5U
#define RP86_FAIL_PREEMPT_A       0x60C1U
#define RP86_FAIL_PREEMPT_B       0x60C2U
#define RP86_FAIL_FINAL_STATE     0x60D1U
#define RP86_FAIL_SCHED_RETURN    0x60E1U
#define RP86_QUEUE_MAGIC          0x5A3CU
#define RP86_SPIN_LIMIT           0xF000U
#define RP86_TASK_STACK_WORDS     160U

/* One-shot #60 physical-progress markers written to the RP86 result port. */
#define RP86_TRACE_MAIN_ENTER     0x6100U
#define RP86_TRACE_SEM_READY      0x6101U
#define RP86_TRACE_QUEUE_READY    0x6102U
#define RP86_TRACE_TASK_A_READY   0x6103U
#define RP86_TRACE_TASK_B_READY   0x6104U
#define RP86_TRACE_SCHED_START    0x6105U
#define RP86_TRACE_TASK_A_ENTER   0x61A0U
#define RP86_TRACE_TASK_A_YIELDS  0x61A1U
#define RP86_TRACE_TASK_A_PREWAIT 0x61A2U
#define RP86_TRACE_TASK_A_PREEMPT 0x61A3U
#define RP86_TRACE_TASK_A_SEM     0x61A4U
#define RP86_TRACE_TASK_A_QUEUE   0x61A5U
#define RP86_TRACE_TASK_B_ENTER   0x61B0U
#define RP86_TRACE_TASK_B_YIELDS  0x61B1U
#define RP86_TRACE_TASK_B_PREWAIT 0x61B2U
#define RP86_TRACE_TASK_B_PREEMPT 0x61B3U
#define RP86_TRACE_TASK_B_SEM     0x61B4U
#define RP86_TRACE_TASK_B_QUEUE   0x61B5U

extern void rp86ValidationPass( void );
extern void rp86ValidationFail( uint16_t code );
extern void rp86ValidationTrace( uint16_t code );

volatile uint16_t rp86_data_anchor = 0x8606U;

static SemaphoreHandle_t xGate;
static QueueHandle_t xQueue;

static volatile uint16_t usYieldA;
static volatile uint16_t usYieldB;
static volatile uint16_t usReadyA;
static volatile uint16_t usReadyB;
static volatile uint16_t usPreemptA;
static volatile uint16_t usPreemptB;
static volatile uint16_t usPreemptSeenA;
static volatile uint16_t usPreemptSeenB;
static volatile uint16_t usSemaphoreWaiting;
static volatile uint16_t usSemaphoreTaken;
static volatile uint16_t usSemaphoreGiven;
static volatile uint16_t usQueueWaiting;
static volatile uint16_t usQueueSeen;
static volatile uint16_t usProducerDone;

static void prvFail( uint16_t usCode )
{
    rp86ValidationFail( usCode );

    for( ;; )
    {
    }
}

void rp86AssertFailed( unsigned short line )
{
    prvFail( ( uint16_t ) ( 0x6F00U | ( ( uint16_t ) line & 0x00FFU ) ) );
}

static void prvTaskA( void * pvParameters )
{
    uint16_t usIndex;
    uint16_t usValue = 0U;

    ( void ) pvParameters;
    rp86ValidationTrace( RP86_TRACE_TASK_A_ENTER );

    for( usIndex = 0U; usIndex < 4U; usIndex++ )
    {
        usYieldA++;
        taskYIELD();
    }

    while( usYieldB < 4U )
    {
        taskYIELD();
    }
    rp86ValidationTrace( RP86_TRACE_TASK_A_YIELDS );

    usReadyA = 1U;
    while( usReadyB == 0U )
    {
        taskYIELD();
    }

    /* No yield/block here: the peer can set usPreemptB only after a tick. */
    usPreemptA = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_A_PREWAIT );
    while( usPreemptB == 0U )
    {
        usPreemptA++;
        if( usPreemptA >= RP86_SPIN_LIMIT )
        {
            prvFail( RP86_FAIL_PREEMPT_A );
        }
    }
    usPreemptSeenA = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_A_PREEMPT );

    usSemaphoreWaiting = 1U;
    if( xSemaphoreTake( xGate, portMAX_DELAY ) != pdTRUE )
    {
        prvFail( RP86_FAIL_SEM_TAKE );
    }
    usSemaphoreTaken = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_A_SEM );

    usQueueWaiting = 1U;
    if( xQueueReceive( xQueue, &usValue, portMAX_DELAY ) != pdPASS )
    {
        prvFail( RP86_FAIL_QUEUE_RECV );
    }
    if( usValue != RP86_QUEUE_MAGIC )
    {
        prvFail( RP86_FAIL_QUEUE_VALUE );
    }
    usQueueSeen = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_A_QUEUE );

    while( usProducerDone == 0U )
    {
        taskYIELD();
    }

    if( ( usYieldA < 4U ) ||
        ( usYieldB < 4U ) ||
        ( usPreemptSeenA == 0U ) ||
        ( usPreemptSeenB == 0U ) ||
        ( ( usPreemptA <= 1U ) && ( usPreemptB <= 1U ) ) ||
        ( usSemaphoreTaken == 0U ) ||
        ( usSemaphoreGiven == 0U ) ||
        ( usQueueSeen == 0U ) )
    {
        prvFail( RP86_FAIL_FINAL_STATE );
    }

    rp86ValidationPass();
    prvFail( RP86_FAIL_FINAL_STATE );
}

static void prvTaskB( void * pvParameters )
{
    uint16_t usIndex;
    uint16_t usValue = RP86_QUEUE_MAGIC;

    ( void ) pvParameters;
    rp86ValidationTrace( RP86_TRACE_TASK_B_ENTER );

    for( usIndex = 0U; usIndex < 4U; usIndex++ )
    {
        usYieldB++;
        taskYIELD();
    }

    while( usYieldA < 4U )
    {
        taskYIELD();
    }
    rp86ValidationTrace( RP86_TRACE_TASK_B_YIELDS );

    usReadyB = 1U;
    while( usReadyA == 0U )
    {
        taskYIELD();
    }

    /* Symmetric peer of Task A's tick-only preemption rendezvous. */
    usPreemptB = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_B_PREWAIT );
    while( usPreemptA == 0U )
    {
        usPreemptB++;
        if( usPreemptB >= RP86_SPIN_LIMIT )
        {
            prvFail( RP86_FAIL_PREEMPT_B );
        }
    }
    usPreemptSeenB = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_B_PREEMPT );

    while( usSemaphoreWaiting == 0U )
    {
        taskYIELD();
    }
    if( xSemaphoreGive( xGate ) != pdTRUE )
    {
        prvFail( RP86_FAIL_SEM_GIVE );
    }
    usSemaphoreGiven = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_B_SEM );

    while( usQueueWaiting == 0U )
    {
        taskYIELD();
    }
    if( xQueueSend( xQueue, &usValue, portMAX_DELAY ) != pdPASS )
    {
        prvFail( RP86_FAIL_QUEUE_SEND );
    }
    usProducerDone = 1U;
    rp86ValidationTrace( RP86_TRACE_TASK_B_QUEUE );

    for( ;; )
    {
        taskYIELD();
    }
}

uint16_t rp86_freertos_main( void )
{
    rp86ValidationTrace( RP86_TRACE_MAIN_ENTER );

    xGate = xSemaphoreCreateBinary();
    if( xGate == NULL )
    {
        return RP86_FAIL_CREATE_SEM;
    }
    rp86ValidationTrace( RP86_TRACE_SEM_READY );

    xQueue = xQueueCreate( 1U, sizeof( uint16_t ) );
    if( xQueue == NULL )
    {
        return RP86_FAIL_CREATE_QUEUE;
    }
    rp86ValidationTrace( RP86_TRACE_QUEUE_READY );

    if( xTaskCreate( prvTaskA, "A", RP86_TASK_STACK_WORDS, NULL, 2U, NULL ) != pdPASS )
    {
        return RP86_FAIL_CREATE_TASK_A;
    }
    rp86ValidationTrace( RP86_TRACE_TASK_A_READY );

    if( xTaskCreate( prvTaskB, "B", RP86_TASK_STACK_WORDS, NULL, 2U, NULL ) != pdPASS )
    {
        return RP86_FAIL_CREATE_TASK_B;
    }
    rp86ValidationTrace( RP86_TRACE_TASK_B_READY );

    rp86ValidationTrace( RP86_TRACE_SCHED_START );
    vTaskStartScheduler();
    return RP86_FAIL_SCHED_RETURN;
}
