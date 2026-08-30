/* Canonical firmware entry point. Runtime policy lives in one named module. */

#include "runtime/canonical_runtime.h"

int main(void) {
    return rp86_canonical_runtime_run();
}
