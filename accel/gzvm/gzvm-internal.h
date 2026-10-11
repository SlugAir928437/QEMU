/*
 * Compatibility shim for the gzvm accelerator.
 *
 * Upstream QEMU migrated the gzvm internal definitions into
 * include/system/gzvm_int.h (QEMU 11.0.0).  Several accelerator source
 * files still include "gzvm-internal.h" after their fork baseline, so this
 * header forwards to the current location.
 */
#ifndef GZVM_INTERNAL_H
#define GZVM_INTERNAL_H

#include "system/gzvm_int.h"

#endif /* GZVM_INTERNAL_H */