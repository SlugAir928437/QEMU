#ifndef GZVM_INT_H
#define GZVM_INT_H

#include "qemu/accel.h"
#include "qemu/typedefs.h"
#include "qemu/thread.h"
#include "qemu/queue.h"
#include "accel/accel-ops.h"
#include "system/gzvm_report.h"
#include "system/memory.h"
#include "linux-headers/linux/gzvm.h"

typedef struct GZVMMemoryUpdate {
    MemoryRegionSection section;
    QSIMPLEQ_ENTRY(GZVMMemoryUpdate) next;
} GZVMMemoryUpdate;

typedef struct gzvm_slot {
    uint64_t start;
    uint64_t size;
    uint8_t *mem;
    uint32_t id;
    uint32_t flags;
} gzvm_slot;

#define GZVM_MAX_MEM_SLOTS    512

struct GZVMState {
    AccelState parent_obj;
    QemuMutex slots_lock;
    gzvm_slot *slots;
    gint *sorted_ids;
    uint32_t nr_active_slots;
    uint32_t nr_slots_allocated;
    int fd;
    int vmfd;
    uint64_t dtb_start;
    uint64_t dtb_size;
    uint64_t gic_dist_base;
    uint64_t gic_redist_base;
    uint64_t gic_redist_size;
    uint64_t ram_base;
    uint32_t msi_vectors;
    QSIMPLEQ_HEAD(, GZVMMemoryUpdate) transaction_add;
    QSIMPLEQ_HEAD(, GZVMMemoryUpdate) transaction_del;
};

struct GZVCPUState {
    int fd;
    struct gzvm_vcpu_run *run;
};

#define GZVCPU(cpu) ((struct GZVCPUState *)(cpu)->accel)

extern bool gzvm_vm_stopped;

int gzvm_create_vm(void);
int gzvm_start_vm(void);
int gzvm_vm_ioctl(int type, void *arg);
int gzvm_vcpu_ioctl(CPUState *cpu, int type, void *arg);
void *gzvm_cpu_thread_fn(void *arg);
int gzvm_arch_put_registers(CPUState *cs, int level);
int gzvm_arch_get_registers(CPUState *cs, int level);
void gzvm_cpu_synchronize_post_reset(CPUState *cpu);
void gzvm_cpu_synchronize_post_init(CPUState *cpu);
gzvm_slot *gzvm_find_slot_by_addr(uint64_t addr);

int gzvm_dev_ioctl(GZVMState *state, int type, void *arg);
void gzvm_signal_update_regions(GZVMState *state);
void gzvm_install_sigsegv_handler(void);
void gzvm_cleanup_mem_state(void);
void gzvm_ioctl_set_state(GZVMState *state);
extern MemoryListener gzvm_io_listener;
extern MemoryListener gzvm_ioeventfd_listener;

static inline void gzvm_slots_lock(GZVMState *state)
{
    qemu_mutex_lock(&state->slots_lock);
}

static inline void gzvm_slots_unlock(GZVMState *state)
{
    qemu_mutex_unlock(&state->slots_lock);
}

int gzvm_handle_mmio_exit(CPUState *cpu, struct gzvm_vcpu_run *run);
int gzvm_handle_system_event(CPUState *cpu, struct gzvm_vcpu_run *run);
int gzvm_handle_fail_entry(CPUState *cpu, struct gzvm_vcpu_run *run);
int gzvm_handle_internal_error(CPUState *cpu, struct gzvm_vcpu_run *run);
int gzvm_handle_unknown_exit(CPUState *cpu, struct gzvm_vcpu_run *run);
void gzvm_init_cpu_signals(void);
void gzvm_init_vcpu_sigsegv(void);
void gzvm_eat_signals(CPUState *cpu);
void gzvm_cpu_kick_self(void);
gzvm_slot *gzvm_find_slot_by_addr_locked(GZVMState *state, uint64_t addr);

#endif
