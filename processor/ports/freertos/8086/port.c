#include "FreeRTOS.h"
#include "task.h"

/* Project-owned NASM glue.  All entry points use the selected near __cdecl ABI. */
extern uint16_t rp86PortGetCodeSegment( void );
extern uint16_t rp86PortGetDataSegment( void );
extern void rp86PortInstallVectors( void );
extern void rp86PortStartFirstTask( void );

static void prvTaskExitError( void )
{
    portDISABLE_INTERRUPTS();

    for( ;; )
    {
        portNOP();
    }
}

StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters )
{
    const StackType_t usCodeSegment = ( StackType_t ) rp86PortGetCodeSegment();
    const StackType_t usDataSegment = ( StackType_t ) rp86PortGetDataSegment();

    /*
     * After the first IRET, the C task must see an ordinary near-call frame:
     * return IP at [SP] and pvParameters at [SP+2].  A task that returns is a
     * programming error and lands in prvTaskExitError().
     */
    *pxTopOfStack = ( StackType_t ) pvParameters;
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) prvTaskExitError;
    pxTopOfStack--;

    /* Hardware interrupt-return frame consumed by IRET. */
    *pxTopOfStack = portINITIAL_SW;                 /* FLAGS: IF=1. */
    pxTopOfStack--;
    *pxTopOfStack = usCodeSegment;                 /* CS. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) pxCode;        /* IP. */
    pxTopOfStack--;

    /*
     * Explicit RP86 software frame.  portasm.asm restores this in the reverse
     * order: BP, DI, SI, DS, ES, DX, CX, BX, AX, then IRET.
     */
    *pxTopOfStack = ( StackType_t ) 0xAAAAU;        /* AX. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0xBBBBU;        /* BX. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0xCCCCU;        /* CX. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0xDDDDU;        /* DX. */
    pxTopOfStack--;
    *pxTopOfStack = usDataSegment;                 /* ES = DGROUP. */
    pxTopOfStack--;
    *pxTopOfStack = usDataSegment;                 /* DS = DGROUP. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x5151U;        /* SI. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0xD1D1U;        /* DI. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0xB0B0U;        /* BP. */

    return pxTopOfStack;
}

BaseType_t xPortStartScheduler( void )
{
    /* RP2350 already owns the 100 Hz source; the workload only installs IVT. */
    portDISABLE_INTERRUPTS();
    rp86PortInstallVectors();
    rp86PortStartFirstTask();

    /* The first-task restore ends in IRET and never returns here. */
    return pdFALSE;
}

void vPortEndScheduler( void )
{
    /* RP86 owns workload stop/restart.  There is no DOS context to restore. */
    portDISABLE_INTERRUPTS();

    for( ;; )
    {
        portNOP();
    }
}
