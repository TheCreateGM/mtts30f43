/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <drm/drm_file.h>

#include "sgpu.h"

int sgpu_major = 0;
int sgpu_ctl_major = 0;
struct mt_global_s *mt_global;
EXPORT_SYMBOL(mt_global);
int total_gpu_num = 1;
int gpu_ids[MAX_GPU];
int gpu_ids_num = 0;
module_param(total_gpu_num, int, 0660);
module_param_array(gpu_ids, int, &gpu_ids_num, 0660);

extern int sgpu_km_procfs_init(void);
extern int sgpu_km_procfs_deinit(void);
void sgpu_finalize(void);
void sgpu_finalize_gpu(int gpu_id);

mt_gpu_t *sgpu_initialize_gpu(int gpu_id) {
    struct file *mt_ctl_filp = NULL;
    struct file *mt_card_filp = NULL;
    struct file *mt_render_filp = NULL;
    struct pci_dev *dev = NULL;
    int dev_cnt = 0;
    int sgpu_id = 0;
    mt_sgpu_t *mt_sgpu_i = NULL;
    mt_gpu_t *mt_gpu_i = NULL;
    char gpu_path[32];
    void *lock = NULL;
    int ret = 0;

    /*
     * In standalone mode, we don't require mtgpu.ko
     * We'll use the DRM devices directly and PCI device info
     */
    sprintf(gpu_path, "/dev/mtgpu.%d", gpu_id);
    mt_ctl_filp = filp_open_check(gpu_path, 2, 0);
    
    /* If mtgpu.ko is available, use it. Otherwise, continue without it. */
    if (mt_ctl_filp == NULL)
    {
        sgpu_printf("[%s] /dev/mtgpu.%d not available, running in standalone mode", __FUNCTION__, gpu_id);
        sgpu_printf("[%s] Some features may be limited without mtgpu.ko", __FUNCTION__);
        /* Continue in standalone mode - don't fail */
        ret = 0; /* Don't set error - we'll handle this case */
    }
    
    sprintf(gpu_path, "/dev/dri/card%d", gpu_id);
    mt_card_filp = filp_open_check(gpu_path, 2, 0);
    if (mt_card_filp == NULL)
    {
        sgpu_printf("[%s] /dev/dri/card%d not available, running in standalone mode", __FUNCTION__, gpu_id);
        ret = 0; /* In standalone mode, DRM devices are optional */
    }
    sprintf(gpu_path, "/dev/dri/renderD%d", 128 + gpu_id);
    mt_render_filp = filp_open_check(gpu_path, 2, 0);
    if (mt_render_filp == NULL)
    {
        sgpu_printf("[%s] /dev/dri/renderD%d not available, running in standalone mode", __FUNCTION__, 128 + gpu_id);
        ret = 0; /* In standalone mode, DRM devices are optional */
    }
    
    /* If we don't have any devices, we can't proceed */
    if (mt_ctl_filp == NULL && mt_card_filp == NULL && mt_render_filp == NULL)
    {
        sgpu_errorf("[%s] No devices available for gpu %d", __FUNCTION__, gpu_id);
        ret = -EFAULT;
        goto err_ret;
    }
    mt_gpu_i = sgpu_kmalloc(sizeof(mt_gpu_t));
    if (!mt_gpu_i)
    {
        sgpu_errorf("[%s] failed to malloc gpu %d", __FUNCTION__, gpu_id);
        ret = -ENOMEM;
        goto err_ret;
    }

    lock = get_spin_lock();
    if (lock == NULL)
    {
        sgpu_errorf("[%s] failed to get spin lock", __FUNCTION__);
        ret = -ENOMEM;
        goto err_ret;
    }

    while ((dev = pci_get_device(PCI_VENDOR_ID_MT, PCI_ANY_ID, dev)) != NULL)
    {
        if (dev->device != DEVICE_ID_MTT_S10 && dev->device != DEVICE_ID_MTT_S30_2_Core &&
            dev->device != DEVICE_ID_MTT_S30_4_Core && dev->device != DEVICE_ID_MTT_S1000M &&
            dev->device != DEVICE_ID_MTT_S4000 && dev->device != DEVICE_ID_MTT_S50 &&
            dev->device != DEVICE_ID_MTT_S60 && dev->device != DEVICE_ID_MTT_S100 &&
            dev->device != DEVICE_ID_MTT_S1000 && dev->device != DEVICE_ID_MTT_S2000 &&
            dev->device != DEVICE_ID_QUYUAN1 && dev->device != DEVICE_ID_MTT_S80 &&
            dev->device != DEVICE_ID_MTT_S70 && dev->device != DEVICE_ID_MTT_S3000)
            continue;
        if (dev_cnt == gpu_id)
            break;
        dev_cnt ++;
    }

    if (dev == NULL)
    {
        sgpu_errorf("[%s] failed to find device for gpu %d", __FUNCTION__, gpu_id);
        ret = -ENOMEM;
        goto err_ret;
    }

    /* 
     * In standalone mode, we don't require mtgpu driver to be bound
     * Check if mtgpu is loaded, but don't fail if it's not
     */
    if (dev->driver != NULL)
    {
        /* If mtgpu driver is bound, verify it's the correct one */
        if (strcmp(dev->driver->name, MTGPU_DRIVER_NAME) != 0)
        {
            sgpu_printf("[%s] gpu %d has driver '%s' instead of '%s', running in standalone mode", 
                  __FUNCTION__, gpu_id, dev->driver->name, MTGPU_DRIVER_NAME);
        } else {
            sgpu_debugf("[%s] gpu %d has mtgpu driver bound", __FUNCTION__, gpu_id);
        }
    } else {
        /* No driver bound - running in standalone mode */
        sgpu_printf("[%s] gpu %d has no driver bound, running in standalone mode", __FUNCTION__, gpu_id);
    }

    sgpu_debugf("[%s] gpu id: %d, device id: 0x%x", __FUNCTION__, gpu_id, dev->device);
    if (dev->device == DEVICE_ID_MTT_S10 || dev->device == DEVICE_ID_MTT_S30_2_Core ||
        dev->device == DEVICE_ID_MTT_S30_4_Core || dev->device == DEVICE_ID_MTT_S1000M ||
        dev->device == DEVICE_ID_MTT_S4000 || dev->device == DEVICE_ID_MTT_S50 ||
        dev->device == DEVICE_ID_MTT_S60 || dev->device == DEVICE_ID_MTT_S100 ||
        dev->device == DEVICE_ID_MTT_S1000 || dev->device == DEVICE_ID_MTT_S2000)
        mt_gpu_i->arch = SUDI;
    else if (dev->device == DEVICE_ID_QUYUAN1 ||  dev->device == DEVICE_ID_MTT_S80 || dev->device == DEVICE_ID_MTT_S70 || dev->device == DEVICE_ID_MTT_S3000)
        mt_gpu_i->arch = QUYUAN;
    else
        mt_gpu_i->arch = UNKNOWN;

    if (mt_gpu_i->arch == UNKNOWN)
    {
        sgpu_errorf("[%s] unknown gpu device arch", __FUNCTION__);
        ret = -ENOMEM;
        goto err_ret;
    }

    mt_gpu_i->mt_ctl = mt_ctl_filp;
    mt_gpu_i->mt_card = mt_card_filp;
    mt_gpu_i->mt_render = mt_render_filp;
    mt_gpu_i->lock = (spinlock_t *)lock;
    mt_gpu_i->opened = false;
    mt_gpu_i->gpu_id = (uint8_t)gpu_id;
    spin_lock_init(mt_gpu_i->lock);
    mt_gpu_i->policy = sgpu_best_effort;

    mt_gpu_i->task_struct = NULL;
    mt_gpu_i->curr_sgpu_id = 0;
    sprintf(mt_gpu_i->thread_name, "mt%d_thread", gpu_id);
    mt_gpu_i->thread_name[THREAD_NAME_LEN] = '\0';
    mt_gpu_i->time_slice_maximum = 1000;
    mt_gpu_i->time_slice_minimum = 500;
    mt_gpu_i->overcommit_ratio = 100;
    mt_gpu_i->max_inst = 16;

    for (sgpu_id = 0; sgpu_id < MAX_SGPU; sgpu_id++)
    {
        lock = get_spin_lock();
        if (lock == NULL)
        {
            sgpu_errorf("[%s] failed to get spin lock", __FUNCTION__);
            ret = -ENOMEM;
            goto err_ret;
        }

        mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
        mt_sgpu_i->lock = (spinlock_t *)lock;
        spin_lock_init(mt_sgpu_i->lock);
        mt_sgpu_i->ref_count = 0;
        mt_sgpu_i->total_memory = 0x200000;
        mt_sgpu_i->free_memory = 0x200000;
        mt_sgpu_i->minor = gpu_id * MAX_SGPU + sgpu_id;
        mt_sgpu_i->container_id[0] = '\0';
        mt_sgpu_i->weight = 1;
        sgpu_init_list_head(&(mt_sgpu_i->tasks));
        // init in sgpu_km_open
        sgpu_spin_lock(mt_sgpu_i->lock);
        mt_sgpu_i->mt_ctl = NULL;
        mt_sgpu_i->mt_card = NULL;
        mt_sgpu_i->mt_render = NULL;
        mt_sgpu_i->fops_mt_ctl = NULL;
        mt_sgpu_i->fops_mt_card = NULL;
        mt_sgpu_i->fops_mt_render = NULL;
        mt_sgpu_i->device_id = 0;
        sgpu_spin_unlock(mt_sgpu_i->lock);
    }

    return mt_gpu_i;

err_ret:
    if (dev != NULL)
        sgpu_kfree(dev);
    sgpu_finalize_gpu(gpu_id);
    return NULL;
}

// initialize gpu group and sgpu instance.
int sgpu_initialize(void)
{
    int i = 0, gpu_id = 0;
    int ret = 0;
    mt_gpu_t *mt_gpu_i = NULL;

    sgpu_printf("Moore Threads sGPU version: %d.%d.%d", SGPU_MAJOR_VERSION, SGPU_MINOR_VERSION, SGPU_BUILD_VERSION);

    mt_global = sgpu_kmalloc(sizeof(*mt_global));
    if (!mt_global)
    {
        sgpu_errorf("[%s] failed to malloc global", __FUNCTION__);
        ret = -ENOMEM;
        goto err_ret;
    }

    if (gpu_ids_num >= 1)
    {
        total_gpu_num = gpu_ids_num;
    }

    /* init 8 GPUs */
    for (i = 0; i < total_gpu_num; i++)
    {
        gpu_id = i;
        if (gpu_ids_num >= 1)
        {
            gpu_id = gpu_ids[i];
        }

        mt_gpu_i = sgpu_initialize_gpu(gpu_id);
        if (mt_gpu_i == NULL)
        {
            ret = -ENOMEM;
            goto err_ret;
        }
        mt_global->gpus[gpu_id] = mt_gpu_i;
    }
    /* 
     * Try to look up mtgpu_drm_driver_fops
     * Note: kallsyms_lookup_name is not exported in newer kernels,
     * so this will fail. Therefore, we set it to NULL and the driver
     * will need mtgpu.ko to be loaded first with proper symbol exports.
     */
    mt_global->mtgpu_drm_driver_fops = NULL; // (const struct file_operations *)sgpu_kallsyms_lookup_name("mtgpu_drm_driver_fops");
err_ret:
    return ret;
}

void sgpu_finalize_gpu(int gpu_id)
{
    int sgpu_id = 0;
    mt_sgpu_t *mt_sgpu_i = NULL;
    mt_gpu_t *mt_gpu_i = NULL;
    sgpu_task_t *task = NULL, *curr_task = NULL;
    
    mt_gpu_i = mt_global->gpus[gpu_id];
    if (mt_gpu_i == NULL)
    {
        return;
    }
    if (mt_gpu_i->task_struct != NULL)
    {
        sgpu_kthread_stop(mt_gpu_i->task_struct);
        mt_gpu_i->task_struct = NULL;
    }

    for (sgpu_id = 0; sgpu_id < MAX_SGPU; sgpu_id++)
    {
        mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
        if (mt_sgpu_i->lock != NULL)
        {
            put_spin_lock(mt_sgpu_i->lock);
            mt_sgpu_i->lock = NULL;
        }
        list_for_each_entry_safe(task, curr_task, &mt_sgpu_i->tasks, list)
        {
            clean_sgpu_task(task, mt_sgpu_i);
        }
    }
    if (mt_gpu_i->mt_ctl != NULL)
    {
        filp_close(mt_gpu_i->mt_ctl, 0);
        mt_gpu_i->mt_ctl = NULL;
    }
    if (mt_gpu_i->mt_card != NULL)
    {
        filp_close(mt_gpu_i->mt_card, 0);
        mt_gpu_i->mt_card = NULL;
    }
    if (mt_gpu_i->mt_render != NULL)
    {
        filp_close(mt_gpu_i->mt_render, 0);
        mt_gpu_i->mt_render = NULL;
    }

    if (mt_gpu_i->lock != NULL)
    {
        put_spin_lock(mt_gpu_i->lock);
        mt_gpu_i->lock = NULL;
    }
    sgpu_kfree(mt_gpu_i);
}

// finalize gpu group and sgpu instance.
void sgpu_finalize(void)
{
    int gpu_id = 0;
    for (gpu_id = 0; gpu_id < MAX_GPU; gpu_id++)
    {
        sgpu_finalize_gpu(gpu_id);
    }
    sgpu_kfree(mt_global);
    return;
}

// mmap
int sgpu_km_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0;
    int (*fops_mmap)(struct file *, struct vm_area_struct *) = NULL;
    int nvctl = -1;
    int ret = 0;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    mt_gpu_i = mt_global->gpus[gpu_id];
    if (!mt_gpu_i)
    {
        sgpu_errorf("[%s] mt_gpu_%d is null", __FUNCTION__, gpu_id);
        return 0;
    }
    mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);

    switch (nvctl)
    {
    case DEV_GPU_CARD:
        fops_mmap = sgpu_get_fops(mt_sgpu_i->fops_mt_card, sgpu_mmap);
        break;
    case DEV_GPU_RENDER:
        fops_mmap = sgpu_get_fops(mt_sgpu_i->fops_mt_render, sgpu_mmap);
        break;
    case DEV_GPU_CTL:
        fops_mmap = sgpu_get_fops(mt_sgpu_i->fops_mt_ctl, sgpu_mmap);
        break;
    }
    if (fops_mmap == NULL)
    {
        sgpu_errorf("[%s] failed to get fops for mmap", __FUNCTION__);
        return -EFAULT;
    }
    ret = (*fops_mmap)(filp, vma);

    return ret;
}

// poll
unsigned int sgpu_km_poll(struct file *filp, struct poll_table_struct *pts)
{
    int major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0;
    int (*fops_poll)(struct file *, struct poll_table_struct *) = NULL;
    int nvctl = -1;
    int ret = 0;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    mt_gpu_i = mt_global->gpus[gpu_id];
    if (!mt_gpu_i)
    {
        sgpu_errorf("[%s] mt_gpu_%d is null", __FUNCTION__, gpu_id);
        return 0;
    }
    mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);

    switch (nvctl)
    {
    case DEV_GPU_CARD:
        fops_poll = sgpu_get_fops(mt_sgpu_i->fops_mt_card, sgpu_poll);
        break;
    case DEV_GPU_RENDER:
        fops_poll = sgpu_get_fops(mt_sgpu_i->fops_mt_render, sgpu_poll);
        break;
    case DEV_GPU_CTL:
        fops_poll = sgpu_get_fops(mt_sgpu_i->fops_mt_ctl, sgpu_poll);
        break;
    }
    if (fops_poll == NULL)
    {
        sgpu_errorf("[%s] failed to get fops for poll", __FUNCTION__);
        return -EFAULT;
    }
    ret = (*fops_poll)(filp, pts);

    sgpu_debugf("[%s] exit = %d", __FUNCTION__, ret);
    return ret;
}

// open
int sgpu_km_open(struct inode *inode, struct file *filp)
{
    unsigned int rdev = 0, major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0, tgid = 0;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;
    int (*fops_open)(struct inode *, struct file *) = NULL;
    int ret = 0;
    struct list_head *curr = NULL;
    sgpu_task_t *new_task = NULL;
    struct inode *tmp_inode = NULL;
    int nvctl = -1;
    struct drm_file *dfile;

    rdev = get_rdev(inode);
    minor = sgpu_get_minor(rdev);
    major = sgpu_get_major(rdev);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);

    sgpu_debugf("[%s] minor=%d, gpu_id=%d, sgpu_id=%d\n", __FUNCTION__, minor, gpu_id, sgpu_id);
    mt_gpu_i = mt_global->gpus[gpu_id];
    if (mt_gpu_i == NULL)
    {
        sgpu_errorf("[%s] failed to open sgpu since gpu is null", __FUNCTION__);
        ret = -EFAULT;
        return ret;
    }

    if (mt_gpu_i->opened == false)
    {
        for (sgpu_id = 0; sgpu_id < MAX_SGPU; sgpu_id++)
        {
            mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
            sgpu_spin_lock(mt_sgpu_i->lock);
            mt_sgpu_i->mt_ctl = (struct inode *)get_file_inode(mt_gpu_i->mt_ctl);
            if (mt_sgpu_i->mt_ctl == NULL)
            {
                sgpu_errorf("[%s] gpu[%d/%d], mtctl inode is null\n", __FUNCTION__, gpu_id, sgpu_id);
                ret = -EFAULT;
                goto error_ret;
            }
            mt_sgpu_i->mt_card = (struct inode *)get_file_inode(mt_gpu_i->mt_card);
            if (mt_sgpu_i->mt_card == NULL)
            {
                sgpu_errorf("[%s] gpu[%d/%d], dri card inode is null\n", __FUNCTION__, gpu_id, sgpu_id);
                ret = -EFAULT;
                goto error_ret;
            }
            mt_sgpu_i->mt_render = (struct inode *)get_file_inode(mt_gpu_i->mt_render);
            if (mt_sgpu_i->mt_render == NULL)
            {
                sgpu_errorf("[%s] gpu[%d/%d], dri render inode is null\n", __FUNCTION__, gpu_id, sgpu_id);
                ret = -EFAULT;
                goto error_ret;
            }
            mt_sgpu_i->fops_mt_ctl = (struct file_operations *)file_op(mt_gpu_i->mt_ctl);
            if (mt_sgpu_i->fops_mt_ctl == NULL)
            {
                sgpu_errorf("[%s] gpu[%d/%d], mtctl ops is null\n", __FUNCTION__, gpu_id, sgpu_id);
                ret = -EFAULT;
                goto error_ret;
            }
            mt_sgpu_i->fops_mt_card = (struct file_operations *)file_op(mt_gpu_i->mt_card);
            if (mt_sgpu_i->fops_mt_card == NULL)
            {
                sgpu_errorf("[%s] gpu[%d/%d], dri card ops is null\n", __FUNCTION__, gpu_id, sgpu_id);
                ret = -EFAULT;
                goto error_ret;
            }
            mt_sgpu_i->fops_mt_render = (struct file_operations *)file_op(mt_gpu_i->mt_render);
            if (mt_sgpu_i->fops_mt_render == NULL)
            {
                sgpu_errorf("[%s] gpu[%d/%d], dri render ops is null\n", __FUNCTION__, gpu_id, sgpu_id);
                ret = -EFAULT;
                goto error_ret;
            }
            sgpu_spin_unlock(mt_sgpu_i->lock);
        }
        mt_gpu_i->opened = true;
    }

    sgpu_id = DEV_SGPU_ID(minor);
    mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);

    tgid = get_tgid();
    sgpu_spin_lock(mt_sgpu_i->lock);
    list_for_each(curr, &mt_sgpu_i->tasks)
    {
        if (((sgpu_task_t *)curr)->tgid == tgid)
        {
            new_task = (sgpu_task_t *)curr;
            break;
        }
    }

    if (new_task == NULL)
    {
        // add a new task instance into list
        new_task = sgpu_kmalloc(sizeof(sgpu_task_t));
        if (!new_task)
        {
            sgpu_errorf("[%s] failed to malloc new task for gpu %d", __FUNCTION__, gpu_id);
            ret = -ENOMEM;
            goto error_ret;
        }
        new_task->tgid = tgid;
        new_task->alloc_memory = 0;

        // for core
        sgpu_init_list_head(&new_task->rgxcmp_cmds);
        new_task->last_sgpu_id = -1;

        atomic_set(&new_task->open_count, 0);
        sgpu_init_list_head(&new_task->items);
        list_add(&new_task->list, &mt_sgpu_i->tasks);
    }

    switch (nvctl)
    {
    case DEV_GPU_CARD:
        fops_open = sgpu_get_fops(mt_sgpu_i->fops_mt_card, sgpu_open);
        tmp_inode = mt_sgpu_i->mt_card;
        break;
    case DEV_GPU_RENDER:
        fops_open = sgpu_get_fops(mt_sgpu_i->fops_mt_render, sgpu_open);
        tmp_inode = mt_sgpu_i->mt_render;
        break;
    case DEV_GPU_CTL:
        // fops_open = sgpu_get_fops(mt_sgpu_i->fops_mt_ctl, sgpu_open);
        filp->private_data = mt_gpu_i->mt_ctl->private_data;
        tmp_inode = mt_sgpu_i->mt_ctl;
        break;
    }
    sgpu_spin_unlock(mt_sgpu_i->lock);
    set_filp(filp, tmp_inode);

    if (fops_open == NULL)
    {
        sgpu_errorf("[%s] failed to find gpu open function\n", __FUNCTION__);
    }
    else
    {
        ret = (*fops_open)(tmp_inode, filp);
        sgpu_debugf("[%s] fops open ret=%d", __FUNCTION__, ret);
        dfile = (struct drm_file *)filp->private_data;
        if (dfile != NULL && nvctl != DEV_GPU_CTL)
            sgpu_debugf("[%s] drm_file.minor=%d, render=%d, pid=%d", __FUNCTION__,
                        dfile->minor->index, drm_is_render_client(dfile),
                        pid_nr(dfile->pid));
    }
    set_filp(filp, inode);
    if (ret == 0)
    {
        cdev_module_get(inode);
        sgpu_ireadcount_inc(tmp_inode);
        atomic_inc(&new_task->open_count);
    }
    return ret;

error_ret:
    sgpu_spin_unlock(mt_sgpu_i->lock);
    return ret;
}

// close
int sgpu_km_close(struct inode *inode, struct file *filp)
{
    unsigned int major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0, tgid = 0;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;
    int (*fops_close)(struct inode *, struct file *) = NULL;
    int ret = 0;
    struct list_head *curr = NULL;
    int nvctl = 0;
    sgpu_task_t *task = NULL;
    struct inode *tmp_inode = NULL;

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    sgpu_debugf("[%s] minor=%d, gpu_id=%d, sgpu_id=%d\n", __FUNCTION__, minor, gpu_id, sgpu_id);
    mt_gpu_i = mt_global->gpus[gpu_id];
    if (mt_gpu_i == NULL)
    {
        sgpu_errorf("[%s] failed to close sgpu since gpu is null\n", __FUNCTION__);
        ret = -EFAULT;
        return ret;
    }

    mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
    switch (nvctl)
    {
    case DEV_GPU_CARD:
        fops_close = sgpu_get_fops(mt_sgpu_i->fops_mt_card, sgpu_release);
        tmp_inode = mt_sgpu_i->mt_card;
        break;
    case DEV_GPU_RENDER:
        fops_close = sgpu_get_fops(mt_sgpu_i->fops_mt_render, sgpu_release);
        tmp_inode = mt_sgpu_i->mt_render;
        break;
    case DEV_GPU_CTL:
        fops_close = sgpu_get_fops(mt_sgpu_i->fops_mt_ctl, sgpu_release);
        tmp_inode = mt_sgpu_i->mt_ctl;
        break;
    }
    if (fops_close == NULL)
    {
        sgpu_errorf("[%s] failed to find gpu close function", __FUNCTION__);
        ret = 0;
    }
    else
    {
        set_filp(filp, tmp_inode);
        if (nvctl != DEV_GPU_CTL)
            ret = (*fops_close)(tmp_inode, filp);
        if (ret != 0)
            sgpu_errorf("[%s] fops_close failed with ret=%d", __FUNCTION__, ret);
        else
            sgpu_ireadcount_dec(tmp_inode);
    }

    set_filp(filp, inode);
    inode = get_inode(filp);
    cdev_module_put(inode);

    tgid = get_tgid();
    sgpu_spin_lock(mt_sgpu_i->lock);
    list_for_each(curr, &mt_sgpu_i->tasks)
    {
        if (((sgpu_task_t *)curr)->tgid == tgid)
        {
            task = (sgpu_task_t *)curr;
            break;
        }
    }
    if (task == NULL)
    {
        sgpu_errorf("[%s] failed to find task by tgid=%d", __FUNCTION__, tgid);
        goto close_ret;
    }
    atomic_dec(&task->open_count);
    if (atomic_read(&task->open_count) <= 0)
    {
        clean_sgpu_task(task, mt_sgpu_i);
    }

close_ret:
    sgpu_spin_unlock(mt_sgpu_i->lock);
    return ret;
}

// assumes that lock is already acquired
void clean_sgpu_task(sgpu_task_t *task, mt_sgpu_t *mt_sgpu_i)
{
    sgpu_task_handle_t *tmp = NULL, *handler = NULL;
    sgpu_task_rgxcmp_cmd_t *rgxcmp_cmd_struct = NULL, *curr_rgxcmp_cmd_struct = NULL;

    if (task != NULL)
    {
        // remove the task instance from list
        list_for_each_entry_safe(handler, tmp, &task->items, list)
        {
            if (handler == NULL)
            {
                continue;
            }
            mt_sgpu_i->free_memory += handler->alloc_memory;
            task->alloc_memory -= handler->alloc_memory;
            list_del(&handler->list);
            sgpu_kfree(handler);
        }
        list_for_each_entry_safe(rgxcmp_cmd_struct, curr_rgxcmp_cmd_struct, &task->rgxcmp_cmds, list)
        {
            if (rgxcmp_cmd_struct == NULL)
            {
                continue;
            }
            list_del(&rgxcmp_cmd_struct->list);
            sgpu_kfree(rgxcmp_cmd_struct);
        }
        list_del(&task->list);
        sgpu_kfree(task);
    }
}

// check if gpu is used
bool is_gpu_used(mt_gpu_t *mt_gpu_i)
{
    int i = 0;
    mt_sgpu_t * mt_sgpu_i = NULL;
    for (; i < MAX_SGPU; i++)
    {
        mt_sgpu_i = &mt_gpu_i->instances[i];
        if (is_inst_used(mt_sgpu_i))
            return 1;
    }

    return 0;
}

// check if sgpu instance is used by tasks
bool is_inst_used(mt_sgpu_t *mt_sgpu_i)
{
    bool ret = false;
    if (mt_sgpu_i != NULL)
    {
        sgpu_spin_lock(mt_sgpu_i->lock);
        if (list_empty(&(mt_sgpu_i->tasks)) == false)
        {
            ret = true;
        }
        sgpu_spin_unlock(mt_sgpu_i->lock);
    }
    return ret;
}

// check if sgpu instance is opened by container.
bool is_inst_opened(mt_sgpu_t *mt_sgpu_i)
{
    bool ret = false;
    if (mt_sgpu_i != NULL)
    {
        sgpu_spin_lock(mt_sgpu_i->lock);
        if (strlen(mt_sgpu_i->container_id) != 0)
        {
            ret = true;
        }
        sgpu_spin_unlock(mt_sgpu_i->lock);
    }
    return ret;
}

static struct file_operations sgpu_km_fops = {
    .owner = THIS_MODULE,
    .poll = sgpu_km_poll,
    .unlocked_ioctl = sgpu_km_unlocked_ioctl,
    .compat_ioctl = sgpu_km_compat_ioctl,
    .mmap = sgpu_km_mmap,
    .open = sgpu_km_open,
    .release = sgpu_km_close,
};

int pre_check(void)
{
    if (total_gpu_num > 8 || total_gpu_num < 0)
    {
        sgpu_errorf("[%s] total_gpu_num need to be an integer in [0,8]\n", __FUNCTION__);
        return -1;
    }
    return 0;
}

// sgpu km init
static int __init sgpu_km_init(void)
{
    int ret = 0;

    ret = pre_check();
    if (ret < 0)
    {
        sgpu_errorf("[%s] failed to pass sGPU pre check\n", __FUNCTION__);
        return ret;
    }
    ret = sgpu_initialize();
    if (ret < 0)
    {
        sgpu_errorf("[%s] failed to initialize sGPU-km\n", __FUNCTION__);
        goto init_err_ret1;
    }
    ret = register_chrdev(0, "sgpu-km", &sgpu_km_fops);
    if (ret < 0)
    {
        sgpu_errorf("[%s] failed to register chrdev sGPU-km\n", __FUNCTION__);
        goto init_err_ret1;
    }
    sgpu_major = ret;
    ret = register_chrdev(0, "sgpu-km-ctl", &sgpu_km_fops);
    if (ret < 0)
    {
        sgpu_errorf("[%s] failed to register chrdev sGPU-km\n", __FUNCTION__);
        goto init_err_ret2;
    }
    sgpu_ctl_major = ret;
    ret = sgpu_km_procfs_init();
    if (ret < 0)
    {
        sgpu_errorf("[%s] failed to init sGPU-km procfs\n", __FUNCTION__);
        goto init_err_ret3;
    }

    return ret;

init_err_ret3:
    unregister_chrdev(sgpu_ctl_major, "sgpu-km-ctl");
init_err_ret2:
    unregister_chrdev(sgpu_major, "sgpu-km");
init_err_ret1:
    sgpu_finalize();

    return ret;
}

// sgpu km exit
static void __exit sgpu_km_exit(void)
{
    sgpu_finalize();
    sgpu_km_procfs_deinit();
    unregister_chrdev(sgpu_ctl_major, "sgpu-km-ctl");
    unregister_chrdev(sgpu_major, "sgpu-km");
}

module_init(sgpu_km_init);
module_exit(sgpu_km_exit);
MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("MooreThreads Corporation");
MODULE_DESCRIPTION("Moore Threads sGPU Kernel Module for MUSA");
MODULE_VERSION("1.1.1");
// MODULE_SOFTDEP("pre: mtgpu"); // Removed for Fedora compatibility testing
