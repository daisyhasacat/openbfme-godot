/* MACPLAY-1 (Sol r3): tests only; see ../libproc.h. KERN_PROC_PID through /proc: the zombie state only. */
#ifndef OPENBFME_LINUX_SYSCTL_SHIM
#define OPENBFME_LINUX_SYSCTL_SHIM
#include "../libproc.h"
#define CTL_KERN 1
#define KERN_PROC 14
#define KERN_PROC_PID 1
struct kinfo_proc {
    struct {
        char p_stat;
    } kp_proc;
};

static inline int sysctl(int *mib, unsigned int count, void *old, size_t *length, void *new_value, size_t new_length) {
    (void)count;
    (void)new_value;
    (void)new_length;
    struct proc_bsdinfo info;
    if (*length != sizeof(struct kinfo_proc) || proc_pidinfo(mib[3], PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
        return -1;
    ((struct kinfo_proc *)old)->kp_proc.p_stat = (char)info.pbi_status;
    return 0;
}
#endif
