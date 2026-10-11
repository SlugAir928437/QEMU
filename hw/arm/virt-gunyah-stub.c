/*
 * Stubs for the gunyah accelerator hooks that hw/arm/virt.c calls
 * unconditionally.
 *
 * virt.c guards every one of these behind gunyah_enabled(), but it still
 * references them at compile time, so the symbols have to exist even when the
 * gunyah accelerator itself is not built.  CONFIG_GH is only set for
 * aarch64-softmmu (see accelerator_targets in meson.build), yet virt.c is
 * common to arm-softmmu and aarch64-softmmu, and Meson cannot use CONFIG_*
 * symbols here (they are GCC-poisoned in this library).  This file is
 * therefore selected by Meson whenever CONFIG_GH is false, exactly like
 * accel/stubs/gunyah-stub.c does for gunyah_allowed and accel/stubs/kvm-stub.c
 * does for the KVM accelerator.
 *
 * Gunyah is a 64-bit ARM hypervisor, so on 32-bit arm-softmmu (and in a build
 * configured with -Dgunyah=disabled) gunyah_enabled() is always false and none
 * of these are ever reached.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/gunyah.h"
#include "system/gunyah_int.h"

GUNYAHState *get_gunyah_state(void)
{
    return NULL;
}

int gunyah_arm_set_dtb(uint64_t dtb_start, uint64_t dtb_size)
{
    (void)dtb_start;
    (void)dtb_size;
    return 0;
}

void gunyah_arm_build_dtb(const struct arm_boot_info *binfo, void *fdt)
{
    (void)binfo;
    (void)fdt;
}
