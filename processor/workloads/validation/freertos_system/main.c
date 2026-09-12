#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "rp86_event.h"

#define RP86_TASK_STACK_WORDS       160U
#define RP86_QUEUE_LENGTH           4U
#define RP86_LED_PERIOD_MS          500U
#define RP86_PRODUCER_PERIOD_MS     300U

#define RP86_FAIL_CREATE_QUEUE      0x6701U
#define RP86_FAIL_CREATE_LED        0x6702U
#define RP86_FAIL_CREATE_PRODUCER   0x6703U
#define RP86_FAIL_CREATE_CONSUMER   0x6704U
#define RP86_FAIL_QUEUE_SEND        0x6711U
#define RP86_FAIL_QUEUE_RECV        0x6712U
#define RP86_FAIL_QUEUE_SEQUENCE    0x6713U
#define RP86_FAIL_SCHED_RETURN      0x67E1U

#define RP86_EVT_BOOT               1U
#define RP86_EVT_LED_ON             2U
#define RP86_EVT_LED_OFF            3U
#define RP86_EVT_QUEUE_SEND         4U
#define RP86_EVT_QUEUE_RECV         5U
#define RP86_EVT_ERROR              6U

typedef struct rp86_freertos_telemetry
{
    volatile uint16_t led_state;
    volatile uint16_t led_toggle_count;
    volatile uint16_t queue_tx_count;
    volatile uint16_t queue_rx_count;
    rp86_event_t last_event;
    volatile uint16_t error_count;
} rp86_freertos_telemetry_t;

typedef struct rp86_freertos_queue_trace
{
    volatile uint16_t armed;
    volatile uint16_t stage;
    volatile uint16_t detail;
} rp86_freertos_queue_trace_t;

extern void rp86SystemFail( uint16_t code );
extern void rp86PortResetTrace( void );

/* Exported intentionally: linker-map symbols are workload-local discovery aids. */
volatile rp86_freertos_telemetry_t gRp86Telemetry;
volatile rp86_freertos_queue_trace_t gRp86QueueTrace;
volatile uint16_t rp86_data_anchor = 0x8667U;

static QueueHandle_t xQueue;
static uint16_t usTelemetrySequence;

void rp86QueueTraceStage( unsigned short stage, unsigned short detail )
{
    if( gRp86QueueTrace.armed != 0U )
    {
        gRp86QueueTrace.stage = ( uint16_t ) stage;
        gRp86QueueTrace.detail = ( uint16_t ) detail;
    }
}

static void prvArmQueueTrace( void )
{
    gRp86QueueTrace.detail = 0U;
    gRp86QueueTrace.stage = 1U;
    gRp86QueueTrace.armed = 1U;
}

static void prvBeginTelemetryUpdateLocked( void )
{
    usTelemetrySequence++;
    gRp86Telemetry.last_event.seq = usTelemetrySequence; /* odd: unstable */
}

static void prvCommitEventLocked( uint16_t usEvent, uint16_t usArg )
{
    gRp86Telemetry.last_event.event = usEvent;
    gRp86Telemetry.last_event.arg = usArg;
    usTelemetrySequence++;
    gRp86Telemetry.last_event.seq = usTelemetrySequence; /* even: stable */
}

/*
 * Temporary #71 scheduler-localization witness:
 *   0 entry, 1 queue, 2 LED created, 3 producer created, 4 consumer created,
 *   5 immediately before vTaskStartScheduler(), 6 consumer task entered,
 *   7 producer task entered, 8 LED task entered,
 *   9 consumer immediately before the one-shot explicit portYIELD(),
 *  10 explicit portYIELD() returned; immediately before xQueueReceive().
 */
static void prvPublishBootStage( uint16_t usStage )
{
    prvBeginTelemetryUpdateLocked();
    prvCommitEventLocked( RP86_EVT_BOOT, usStage );
}

static void prvPublishTaskStage( uint16_t usStage )
{
    taskENTER_CRITICAL();
    prvPublishBootStage( usStage );
    taskEXIT_CRITICAL();
}

static uint16_t prvPreSchedulerFail( uint16_t usCode )
{
    prvBeginTelemetryUpdateLocked();
    gRp86Telemetry.error_count++;
    prvCommitEventLocked( RP86_EVT_ERROR, usCode );
    return usCode;
}

static void prvFatal( uint16_t usCode )
{
    taskENTER_CRITICAL();
    prvBeginTelemetryUpdateLocked();
    gRp86Telemetry.error_count++;
    prvCommitEventLocked( RP86_EVT_ERROR, usCode );
    taskEXIT_CRITICAL();

    rp86SystemFail( usCode );

    for( ;; )
    {
    }
}

void rp86AssertFailed( unsigned short line )
{
    prvFatal( ( uint16_t ) ( 0x6F00U | ( ( uint16_t ) line & 0x00FFU ) ) );
}

static void prvLedTask( void * pvParameters )
{
    ( void ) pvParameters;
    prvPublishTaskStage( 8U );

    for( ;; )
    {
        taskENTER_CRITICAL();
        prvBeginTelemetryUpdateLocked();
        gRp86Telemetry.led_state ^= 1U;
        gRp86Telemetry.led_toggle_count++;
        prvCommitEventLocked(
            ( gRp86Telemetry.led_state != 0U ) ? RP86_EVT_LED_ON : RP86_EVT_LED_OFF,
            gRp86Telemetry.led_toggle_count );
        taskEXIT_CRITICAL();

        vTaskDelay( pdMS_TO_TICKS( RP86_LED_PERIOD_MS ) );
    }
}

static void prvProducerTask( void * pvParameters )
{
    uint16_t usValue = 0U;

    ( void ) pvParameters;
    prvPublishTaskStage( 7U );

    for( ;; )
    {
        usValue++;
        if( xQueueSend( xQueue, &usValue, portMAX_DELAY ) != pdPASS )
        {
            prvFatal( RP86_FAIL_QUEUE_SEND );
        }

        taskENTER_CRITICAL();
        prvBeginTelemetryUpdateLocked();
        gRp86Telemetry.queue_tx_count++;
        prvCommitEventLocked( RP86_EVT_QUEUE_SEND, usValue );
        taskEXIT_CRITICAL();

        vTaskDelay( pdMS_TO_TICKS( RP86_PRODUCER_PERIOD_MS ) );
    }
}

static void prvConsumerTask( void * pvParameters )
{
    uint16_t usValue = 0U;
    uint16_t usExpected = 1U;
    uint16_t usYieldProbeDone = 0U;

    ( void ) pvParameters;
    prvPublishTaskStage( 6U );

    for( ;; )
    {
        if( usYieldProbeDone == 0U )
        {
            prvPublishTaskStage( 9U );
            portYIELD();
            usYieldProbeDone = 1U;
            prvPublishTaskStage( 10U );

            /*
             * The explicit-yield round trip is now proven.  Reset the port
             * witness so the next INT 80h, if xQueueReceive reaches it, is the
             * one captured alongside the queue-blocking trace below.
             */
            rp86PortResetTrace();
            prvArmQueueTrace();
        }

        if( xQueueReceive( xQueue, &usValue, portMAX_DELAY ) != pdPASS )
        {
            prvFatal( RP86_FAIL_QUEUE_RECV );
        }
        if( usValue != usExpected )
        {
            prvFatal( RP86_FAIL_QUEUE_SEQUENCE );
        }
        usExpected++;

        taskENTER_CRITICAL();
        prvBeginTelemetryUpdateLocked();
        gRp86Telemetry.queue_rx_count++;
        prvCommitEventLocked( RP86_EVT_QUEUE_RECV, usValue );
        taskEXIT_CRITICAL();
    }
}

uint16_t rp86_freertos_system_main( void )
{
    usTelemetrySequence = 0U;
    gRp86QueueTrace.armed = 0U;
    gRp86QueueTrace.stage = 0U;
    gRp86QueueTrace.detail = 0U;
    prvPublishBootStage( 0U );

    xQueue = xQueueCreate( RP86_QUEUE_LENGTH, sizeof( uint16_t ) );
    if( xQueue == NULL )
    {
        return prvPreSchedulerFail( RP86_FAIL_CREATE_QUEUE );
    }
    prvPublishBootStage( 1U );

    if( xTaskCreate( prvLedTask, "LED", RP86_TASK_STACK_WORDS, NULL, 1U, NULL ) != pdPASS )
    {
        return prvPreSchedulerFail( RP86_FAIL_CREATE_LED );
    }
    prvPublishBootStage( 2U );

    if( xTaskCreate( prvProducerTask, "PROD", RP86_TASK_STACK_WORDS, NULL, 2U, NULL ) != pdPASS )
    {
        return prvPreSchedulerFail( RP86_FAIL_CREATE_PRODUCER );
    }
    prvPublishBootStage( 3U );

    if( xTaskCreate( prvConsumerTask, "CONS", RP86_TASK_STACK_WORDS, NULL, 3U, NULL ) != pdPASS )
    {
        return prvPreSchedulerFail( RP86_FAIL_CREATE_CONSUMER );
    }
    prvPublishBootStage( 4U );

    prvPublishBootStage( 5U );
    vTaskStartScheduler();
    return prvPreSchedulerFail( RP86_FAIL_SCHED_RETURN );
}
