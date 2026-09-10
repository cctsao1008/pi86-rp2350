/*
 * Freestanding 16-bit C validation payload for Issue #58.
 *
 * No hosted C runtime or DOS service is permitted here.  Keep the first
 * workload intentionally limited to 16-bit integer operations so compiler
 * helper calls remain easy to audit on the Intel 8086 baseline.
 */

typedef unsigned short rp86_u16;

volatile rp86_u16 rp86_data_anchor = 0x1357u;
volatile rp86_u16 rp86_bss_probe;

rp86_u16 __cdecl rp86_c16_add( rp86_u16 left, rp86_u16 right )
{
    return ( rp86_u16 ) ( left + right );
}

rp86_u16 __cdecl rp86_c16_main( void )
{
    rp86_u16 local_value = 0x0101u;
    volatile rp86_u16 *anchor = &rp86_data_anchor;

    /* Startup must clear the linker-defined BSS range before C entry.  Keep
     * this check as native execution evidence rather than trusting SRAM or
     * loader state. */
    if( rp86_bss_probe != 0u ) {
        return 0xB551u;
    }

    rp86_bss_probe = 0x0022u;

    return ( rp86_u16 )
           ( rp86_c16_add( *anchor, rp86_bss_probe ) + local_value );
}
