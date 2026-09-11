#include <stdbool.h>

#include "bus/processor_bus.h"

/* Host-side workload_executor tests exercise production policy without RP2350
 * GPIO. The physical INTR edge is covered by hardware validation under #59. */
void rp86_processor_bus_set_intr(bool asserted) {
    (void)asserted;
}
