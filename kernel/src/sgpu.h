/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#pragma once
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/dma-fence.h>
#include "version.h"
#include "utils.h"
#include "mtgpu.h"

#define PROCFS_ROOT_DIR "sgpu_km"
#define PROCFS_CORES_NODE "cores"
#define PROCFS_INST_CTL_NODE "inst_ctl"
#define PROCFS_MAX_INST_NODE "max_inst"
#define PROCFS_POLICY_NODE "policy"
#define PROCFS_TIME_SLICE_NODE "time_slice"
#define PROCFS_OVERCOMMIT_RATIO "overcommit_ratio"
#define NODE_NAME_LEN 12
#define PROCFS_MAJOR_NODE "major"
#define PROCFS_MAJOR_CTL_NODE "misc_major"
#define THREAD_NAME_LEN 10

#define MAX_GPU 8 // for initializing proc_insts and mt_global_s
#define MAX_SGPU 16

/*
 * sGPU minor
 *  x    xxx    xxxx
 *  |    \-/    \--/
 * TYPE GPU_ID SGPU_ID
 *
 * TYPE: 0x0, /dev/dri/card0
 *       0x1, /dev/dri/renderD128
 * GPU_ID: Real MT GPU ID
 * SGPU_ID: sGPU instance ID
 */
#define DEV_GPU_CARD 0x0
#define DEV_GPU_RENDER 0x1
#define DEV_GPU_CTL 0x2

enum device_arch
{
    SUDI,
    QUYUAN,
    UNKNOWN,
};

enum sgpu_core_policy
{
    sgpu_best_effort,     // core is not limited
    sgpu_fair_preemption, // core is limited by soft limit
    sgpu_fair_fixed,      // core is limited by hard limit
};

struct sgpu_meminfo_entry
{
    int pid;
    uint64_t mem_size;
};

struct sgpu_meminfo
{
    unsigned int num;
    uint64_t free_size;
    struct sgpu_meminfo_entry *entries;
};

typedef struct sgpu_task_handle_s sgpu_task_handle_t;
struct sgpu_task_handle_s
{
    struct list_head list; /* list for the handler */
    uint64_t handle;       /* handle ID */
    uint64_t alloc_memory; /* allocated memory for each handle */
};

typedef struct sgpu_task_rgxcmp_cmd_s sgpu_task_rgxcmp_cmd_t;
struct sgpu_task_rgxcmp_cmd_s
{
    struct list_head list;
    int32_t real_check_fence; /* check fence from userspace */
    int32_t fake_check_fence; /* check fence created and sent to kernel by us */
    struct dma_fence *fake_fence;
    spinlock_t *fence_lock; /* spinlock */

    bool submitted; /* whether to submit to kernel */
};

typedef struct sgpu_task_s sgpu_task_t;
struct sgpu_task_s
{
    struct list_head list;  /* list for the task */
    int tgid;               /* thread group ID */
    atomic_t open_count;    /* open count */
    uint64_t alloc_memory;  /* allocated memory for task */
    struct list_head items; /* list head for handler */

    // for core schedule
    struct list_head rgxcmp_cmds; /* rgx compute commands */
    int last_sgpu_id;
};

typedef struct mt_sgpu_s mt_sgpu_t;
struct mt_sgpu_s
{
    uint32_t minor;                       /* minor */
    char container_id[NODE_NAME_LEN + 1]; /* container ID for this sGPU */
    struct inode *mt_ctl;                 /* mtgpu control device /dev/mtgpu.0 */
    struct inode *mt_card;                /* mtgpu card device /dev/dri/card0 */
    struct inode *mt_render;              /* mtgpu render device /dev/dri/renderD128 */
    struct file_operations *fops_mt_ctl;
    struct file_operations *fops_mt_card;
    struct file_operations *fops_mt_render;
    spinlock_t *lock; /* spinlock */

    uint16_t ref_count;    /* reference count */
    uint64_t total_memory; /* total memory, Byte */
    uint64_t free_memory;  /* free memory, Byte */

    uint16_t device_id;     /* device ID */
    struct list_head tasks; /* list head for the tasks */

    // for core schedule
    int weight;
};

typedef struct mt_gpu_s mt_gpu_t;
struct mt_gpu_s
{
    uint8_t gpu_id; /* GPU ID, useless */
    enum device_arch arch;
    struct file *mt_ctl;
    struct file *mt_card;
    struct file *mt_render;
    spinlock_t *lock;              /* spinlock, useless */
    mt_sgpu_t instances[MAX_SGPU]; /* instances */
    bool opened;
    enum sgpu_core_policy policy; /* the core policy, default sgpu_best_effort */
    uint64_t mem_total_size;

    // for core schedule
    void *task_struct;                     /* os_kthread_run for core */
    char thread_name[THREAD_NAME_LEN + 1]; /* os_kthread_run thread name */
    int max_inst;                          /* total inst num, default 16 */
    int curr_sgpu_id;                       /* current running sGPU id */
    int time_slice_maximum;                /* time slice length upper limit*/
    int time_slice_minimum;                /* time slice length lower limit*/
    int overcommit_ratio;                  /* memory overcommit ratio, default 100 */
};

struct mt_global_s
{
    mt_gpu_t *gpus[MAX_GPU];
    const struct file_operations *mtgpu_drm_driver_fops;
};

long sgpu_km_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long sgpu_km_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

void *get_spin_lock(void);
void put_spin_lock(void *lock);
void clean_sgpu_task(sgpu_task_t *task, mt_sgpu_t *mt_sgpu_i);

long memory_info_ioctl(mt_ioctl_srvkm_cmd_t *cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i);
long memory_info_pkd_ioctl(mt_ioctl_srvkm_cmd_t *cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i);
long memory_free_ioctl(mt_ioctl_srvkm_cmd_t *cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i);
long memory_alloc_ioctl(mt_ioctl_srvkm_cmd_t *cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i);

int core_rgxcmp_sched(void *ptr);
enum device_types get_device_type(uint16_t device_id);
int sgpu_clear_set_pfn(void *priv, unsigned long pfn);
int sgpu_km_vma_fault(void *vmf);
bool is_gpu_used(mt_gpu_t *mt_gpu_i);
bool is_inst_used(mt_sgpu_t *mt_sgpu_i);
bool is_inst_opened(mt_sgpu_t *mt_sgpu_i);

int create_fence(int *fd_to_resolv, struct dma_fence *fence, spinlock_t *fence_lock);

int mtml_memory_ioctl(ipcMsg_t *mtml_ipc, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i);
