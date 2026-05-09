/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/delay.h>
#include <linux/pid_namespace.h>
#include <linux/sched.h>
#include <drm/drm_file.h>
#include <linux/kallsyms.h>
#include <linux/fdtable.h>
#include <linux/errno.h>

#include "sgpu.h"

extern struct mt_global_s *mt_global;

long sgpu_km_pre_ioctl(struct file *filp, fops_types_e type, unsigned int cmd, unsigned long arg, bool *r);
long sgpu_km_post_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

// unlocked ioctl
long sgpu_km_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long ret = 0;
    long (*fops_unlocked_ioctl)(struct file *, unsigned int, unsigned long) = NULL;
    bool fail = false;
    int major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0;
    struct file_operations *fops_nvctl = NULL;
    int nvctl = -1;

    ret = sgpu_km_pre_ioctl(filp, sgpu_unlocked_ioctl, cmd, arg, &fail);
    if (ret != 0 || fail == true)
    {
        return ret;
    }

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    sgpu_debugf("[%s] major=%d, minor=%d, type=0x%x, gpu_id=%d, sgpu_id=%d", __FUNCTION__, major, minor, nvctl, gpu_id, sgpu_id);
    if (mt_global->gpus[gpu_id] == NULL)
    {
        return -EFAULT;
    }

    switch (nvctl)
    {
    case DEV_GPU_CARD:
        fops_nvctl = mt_global->gpus[gpu_id]->instances[sgpu_id].fops_mt_card;
        break;
    case DEV_GPU_RENDER:
        fops_nvctl = mt_global->gpus[gpu_id]->instances[sgpu_id].fops_mt_render;
        break;
    case DEV_GPU_CTL:
        fops_nvctl = mt_global->gpus[gpu_id]->instances[sgpu_id].fops_mt_ctl;
        break;
    }

    fops_unlocked_ioctl = sgpu_get_fops(fops_nvctl, sgpu_unlocked_ioctl);
    if (fops_unlocked_ioctl != NULL)
    {
        ret = (*fops_unlocked_ioctl)(filp, cmd, arg);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed in ioctl: %ld\n", __FUNCTION__, ret);
            return ret;
        }
    }
    ret = sgpu_km_post_ioctl(filp, cmd, arg);
    sgpu_debugf("[%s] exit with ret=%d", __FUNCTION__, ret);
    return ret;
}

// compat ioctl
long sgpu_km_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long ret = 0;
    long (*fops_compat_ioctl)(struct file *, unsigned int, unsigned long) = NULL;
    bool fail = false;
    int major, minor = 0;
    int gpu_id = 0, sgpu_id = 0;
    struct file_operations *fops_nvctl = NULL;
    int nvctl = -1;

    ret = sgpu_km_pre_ioctl(filp, sgpu_compat_ioctl, cmd, arg, &fail);
    if (ret != 0 || fail == true)
    {
        return ret;
    }

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    sgpu_debugf("[%s] major=%d, minor=%d, type=0x%x, gpu_id=%d, sgpu_id=%d", __FUNCTION__, major, minor, nvctl, gpu_id, sgpu_id);
    if (mt_global->gpus[gpu_id] == NULL)
    {
        return -EFAULT;
    }

    switch (nvctl)
    {
    case DEV_GPU_CARD:
        fops_nvctl = mt_global->gpus[gpu_id]->instances[sgpu_id].fops_mt_card;
        break;
    case DEV_GPU_RENDER:
        fops_nvctl = mt_global->gpus[gpu_id]->instances[sgpu_id].fops_mt_render;
        break;
    case DEV_GPU_CTL:
        fops_nvctl = mt_global->gpus[gpu_id]->instances[sgpu_id].fops_mt_ctl;
        break;
    }

    fops_compat_ioctl = sgpu_get_fops(fops_nvctl, sgpu_compat_ioctl);
    if (fops_compat_ioctl != NULL)
    {
        ret = (*fops_compat_ioctl)(filp, cmd, arg);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed in ioctl: %ld\n", __FUNCTION__, ret);
            return ret;
        }
    }
    ret = sgpu_km_post_ioctl(filp, cmd, arg);
    sgpu_debugf("[%s] exit with ret=%d", __FUNCTION__, ret);
    return ret;
}

// pre ioctl
long sgpu_km_pre_ioctl(struct file *filp, fops_types_e type, unsigned int cmd, unsigned long arg, bool *r)
{
    unsigned int major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0, tgid = 0;
    int nvctl = -1;
    long ret = 0;
    mt_ioctl_srvkm_cmd_t *srvkm_cmd = NULL;
    mt_ioctl_memory_alloc_in_t *mem_alloc = NULL;
    mt_ioctl_memory_alloc_out_t *mem_alloc_out = NULL;
    mt_ioctl_compute_in_t *rgxcmp_kick = NULL;
    mt_ioctl_compute_out_t *rgxcmp_kick_out = NULL;
    void *arg_ptr = (void *)arg;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;
    sgpu_task_t *task = NULL;
    struct list_head *curr = NULL;
    size_t arg_size = 0;
    int arg_cmd = 0;
    struct file *tfile = NULL;
    struct drm_file *dfile = NULL;
    struct files_struct *files = current->files;
    struct fdtable *fdt = NULL;
    sgpu_task_rgxcmp_cmd_t *rgxcmp_cmd = NULL;
    mt_ioctl_dma_dmatransfer_t *dma_transfer = NULL;
    mt_ioctl_rgxtq2_transfer2_in_t *rgxtq2_transfer = NULL;

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    sgpu_debugf("[%s] major=%d, minor=%d, type=0x%x", __FUNCTION__, major, minor, nvctl);
    // ignore devices:
    // - /dev/mtgpu.x
    if (nvctl == DEV_GPU_CTL)
    {
        return ret;
    }

    arg_size = _IOC_SIZE(cmd);
    arg_cmd = _IOC_NR(cmd);
    sgpu_debugf("[%s] ioctl command: 0x%x", __FUNCTION__, arg_cmd);
    if (arg_cmd != MT_IOCTL_PVR_SRVKM)
        return ret;
    mt_gpu_i = mt_global->gpus[gpu_id];
    if (mt_gpu_i == NULL)
    {
        return ret;
    }
    mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
    tgid = get_tgid();

    srvkm_cmd = sgpu_kmalloc(sizeof(mt_ioctl_srvkm_cmd_t));
    if (srvkm_cmd == NULL)
    {
        sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
        ret = -ENOMEM;
        return ret;
    }
    ret = sgpu_copy_from_user(srvkm_cmd, arg_ptr, sizeof(mt_ioctl_srvkm_cmd_t));
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto pre_return;
    }
    sgpu_debugf("[%s] bridge_id = 0x%x, bridge_func_id = 0x%x", __FUNCTION__,
                srvkm_cmd->bridge_id, srvkm_cmd->bridge_func_id);

    if (srvkm_cmd->bridge_id == MT_IOCTL_DMA &&
        srvkm_cmd->bridge_func_id == MT_IOCTL_DMA_TRANSFER)
    {
        dma_transfer = sgpu_kmalloc(srvkm_cmd->in_data_size);
        if (dma_transfer == NULL)
        {
            sgpu_errorf("[%s] failed to malloc for dma transfer\n", __FUNCTION__);
            ret = -ENOMEM;
            goto pre_return;
        }
        ret = sgpu_copy_from_user(dma_transfer, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            goto pre_return1;
        }

        // sgpu_debugf("[%s] alloc memory size: %d", __FUNCTION__, mem_alloc->uiSize);
        fdt = files_fdtable(files);
        if (fdt == NULL)
        {
            sgpu_errorf("[%s] failed to get fdtable", __FUNCTION__);
        }
        tfile = fdt->fd[dma_transfer->hUpdateTimeline];
        if (tfile == NULL)
            sgpu_errorf("[%s] failed to get file with fd[%d]", __FUNCTION__, dma_transfer->hUpdateTimeline);
        else
            tfile->f_op = mt_global->mtgpu_drm_driver_fops;
    } else if (srvkm_cmd->bridge_id == MT_IOCTL_RGXTQ2 &&
            srvkm_cmd->bridge_func_id == MT_IOCTL_RGXTQ2_RGXTDMSUBMITTRANSFER2) {
        rgxtq2_transfer = sgpu_kmalloc(srvkm_cmd->in_data_size);
        if (rgxtq2_transfer == NULL) 
        {
            sgpu_errorf("[%s] failed to malloc for rgxtq2 transfer\n", __FUNCTION__);
            ret = -ENOMEM;
            goto pre_return;
        }
        ret = sgpu_copy_from_user(rgxtq2_transfer, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            goto pre_return1;
        }

        // sgpu_debugf("[%s] alloc memory size: %d", __FUNCTION__, mem_alloc->uiSize);
        fdt = files_fdtable(files);
        if (fdt == NULL)
        {
            sgpu_errorf("[%s] failed to get fdtable", __FUNCTION__);
        }
        tfile = fdt->fd[rgxtq2_transfer->hUpdateTimeline];
        if (tfile == NULL)
            sgpu_errorf("[%s] failed to get file with fd[%d]", __FUNCTION__, rgxtq2_transfer->hUpdateTimeline);
        else
            tfile->f_op = mt_global->mtgpu_drm_driver_fops;
    } else if (srvkm_cmd->bridge_id == MT_IOCTL_MM &&
             srvkm_cmd->bridge_func_id == MT_IOCTL_MM_PHYSMEMNEWRAMBACKEDPMR)
    {
        mem_alloc = sgpu_kmalloc(srvkm_cmd->in_data_size);
        if (mem_alloc == NULL)
        {
            sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
            ret = -ENOMEM;
            goto pre_return;
        }
        ret = sgpu_copy_from_user(mem_alloc, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            goto pre_return1;
        }

        sgpu_debugf("[%s] alloc memory size: %d", __FUNCTION__, mem_alloc->uiSize);
        if (mt_sgpu_i->free_memory < mem_alloc->uiSize)
        {
            *r = true;
            mem_alloc_out = sgpu_kmalloc(srvkm_cmd->out_data_size);
            if (mem_alloc_out == NULL)
            {
                sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
                ret = -ENOMEM;
                goto pre_return1;
            }
            mem_alloc_out->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
            ret = sgpu_copy_to_user((void *)(srvkm_cmd->out_data_ptr), mem_alloc_out, srvkm_cmd->out_data_size);
            if (ret != 0)
            {
                sgpu_errorf("[%s] failed to copy to user: %ld\n", __FUNCTION__, ret);
            }
            sgpu_kfree(mem_alloc_out);
        }
    }
    else if (srvkm_cmd->bridge_id == MT_IOCTL_RGXCMP &&
             srvkm_cmd->bridge_func_id == MT_IOCTL_RGXCMP_RGXKICKCDM2)
    {
        rgxcmp_kick = sgpu_kmalloc(srvkm_cmd->in_data_size);
        if (rgxcmp_kick == NULL)
        {
            sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
            goto pre_return;
        }
        ret = sgpu_copy_from_user(rgxcmp_kick, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            ret = 0;
            goto pre_return1;
        }
        rgxcmp_kick_out = sgpu_kmalloc(srvkm_cmd->out_data_size);
        if (rgxcmp_kick_out == NULL)
        {
            sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
            goto pre_return2;
        }
        ret = sgpu_copy_from_user(rgxcmp_kick_out, (void *)(srvkm_cmd->out_data_ptr), srvkm_cmd->out_data_size);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            ret = 0;
            goto pre_return2;
        }

        fdt = files_fdtable(files);
        tfile = fdt->fd[rgxcmp_kick->hUpdateTimeline];
        if (tfile == NULL)
        {
            sgpu_errorf("[%s] failed to get file with fd[%d]", __FUNCTION__, rgxcmp_kick->hUpdateTimeline);
        }
        else
        {
            dfile = (struct drm_file *)tfile->private_data;
            if (dfile == NULL)
            {
                sgpu_errorf("[%s] private_data == NULL", __FUNCTION__);
            }
            else
            {
                sgpu_debugf("[%s] drm_file.minor=%d, render=%d, pid=%d, timeline=%d", __FUNCTION__,
                            dfile->minor->index, drm_is_render_client(dfile),
                            pid_nr(dfile->pid), rgxcmp_kick->hUpdateTimeline);
                if (dfile->driver_priv == NULL)
                    sgpu_errorf("[%s] driver_priv == NULL", __FUNCTION__);
            }
            tfile->f_op = mt_global->mtgpu_drm_driver_fops;
        }
        if (mt_gpu_i->policy == sgpu_best_effort)
        {
            goto pre_return2;
        }
        sgpu_spin_lock(mt_sgpu_i->lock);
        list_for_each(curr, &mt_sgpu_i->tasks)
        {
            if (((sgpu_task_t *)curr)->tgid == tgid)
            {
                task = (sgpu_task_t *)curr;
                break;
            }
        }
        sgpu_spin_unlock(mt_sgpu_i->lock);
        if (task == NULL)
        {
            sgpu_errorf("[%s] failed to find task with tgid: %d", __FUNCTION__, tgid);
            goto pre_return2;
        }
        sgpu_debugf("[%s] hCheckFence = %d", __FUNCTION__, rgxcmp_kick->hCheckFenceFd);
        rgxcmp_cmd = sgpu_kmalloc(sizeof(sgpu_task_rgxcmp_cmd_t));
        if (rgxcmp_cmd == NULL)
        {
            sgpu_errorf("[%s] failed to malloc for sgpu task rgxcmd_cmd", __FUNCTION__);
            goto pre_return2;
        }
        rgxcmp_cmd->real_check_fence = rgxcmp_kick->hCheckFenceFd;
        rgxcmp_cmd->fake_fence = sgpu_kmalloc(sizeof(struct dma_fence));
        rgxcmp_cmd->fence_lock = (spinlock_t *)get_spin_lock();
        spin_lock_init(rgxcmp_cmd->fence_lock);

        // Add rgxcmp entry into pending list
        ret = create_fence(&rgxcmp_kick->hCheckFenceFd, rgxcmp_cmd->fake_fence, rgxcmp_cmd->fence_lock);
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to create fake returned fence: %ld\n", __FUNCTION__, ret);
        }
        rgxcmp_cmd->fake_check_fence = rgxcmp_kick->hCheckFenceFd;
        sgpu_spin_lock(mt_sgpu_i->lock);
        list_add(&rgxcmp_cmd->list, &task->rgxcmp_cmds);
        sgpu_spin_unlock(mt_sgpu_i->lock);
        sgpu_copy_to_user((void *)(srvkm_cmd->in_data_ptr), rgxcmp_kick, srvkm_cmd->in_data_size);

        // Kick the schedule
        if (mt_gpu_i->task_struct == NULL)
        {
            mt_gpu_i->task_struct = sgpu_kthread_run(core_rgxcmp_sched, mt_gpu_i, (char *)(mt_gpu_i->thread_name));
        }
    }
    // XXX: For CDM kick out, we kick command without sched
pre_return2:
    if (rgxcmp_kick_out)
        sgpu_kfree(rgxcmp_kick_out);
pre_return1:
    if (mem_alloc)
        sgpu_kfree(mem_alloc);
    if (rgxcmp_kick)
        sgpu_kfree(rgxcmp_kick);
pre_return:
    if (dma_transfer)
        sgpu_kfree(dma_transfer);
    if (rgxtq2_transfer)
        sgpu_kfree(rgxtq2_transfer);
    if (ret == 0)
    {
        ret = sgpu_copy_to_user(arg_ptr, srvkm_cmd, sizeof(mt_ioctl_srvkm_cmd_t));
        if (ret != 0)
            sgpu_errorf("[%s] failed to copy to user: %ld\n", __FUNCTION__, ret);
    }
    sgpu_kfree(srvkm_cmd);
    return ret;
}

// post ioctl
long sgpu_km_post_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    unsigned int major = 0, minor = 0;
    int gpu_id = 0, sgpu_id = 0, tgid = 0;
    int nvctl = -1;
    long ret = 0;
    mt_ioctl_srvkm_cmd_t *srvkm_cmd = NULL;
    void *arg_ptr = (void *)arg;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;
    size_t arg_size = 0;
    int arg_cmd = 0;
    struct file *tfile = NULL;
    struct files_struct *files = current->files;
    struct fdtable *fdt;
    mt_ioctl_compute_in_t *rgxcmp_kick = NULL;
    sgpu_task_t *task = NULL;
    sgpu_task_rgxcmp_cmd_t *rgxcmp_cmd = NULL;
    struct list_head *curr = NULL;
    struct ipcMsg_st *mtml_ipc = NULL;
    mt_ioctl_dma_dmatransfer_t *dma_transfer = NULL;
    mt_ioctl_rgxtq2_transfer2_in_t *rgxtq2_transfer = NULL;

    arg_size = _IOC_SIZE(cmd);
    arg_cmd = _IOC_NR(cmd);

    minor = get_file_minor(filp);
    major = get_file_major(filp);
    nvctl = DEV_TYPE(major, minor);
    gpu_id = DEV_GPU_ID(minor);
    sgpu_id = DEV_SGPU_ID(minor);

    tgid = get_tgid();
    mt_gpu_i = mt_global->gpus[gpu_id];
    if (mt_gpu_i == NULL)
    {
        ret = -EFAULT;
        return ret;
    }
    mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
    if (nvctl == DEV_GPU_CTL)
    {
        // /dev/mtgpu.x
        // only memory total size
        sgpu_debugf("[%s] ioctl command on misc device: 0x%x", __FUNCTION__, arg_cmd);
        if (arg_cmd != MTGPU_IPC_IOCTL_MESSAGE_TRANSMIT)
            return ret;
        mtml_ipc = sgpu_kmalloc(sizeof(ipcMsg_t));
        if (mtml_ipc == NULL)
        {
            sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
            ret = -ENOMEM;
            return ret;
        }
        ret = sgpu_copy_from_user(mtml_ipc, arg_ptr, sizeof(ipcMsg_t));
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            sgpu_kfree(mtml_ipc);
            return ret;
        }
        sgpu_debugf("[%s] ipc event on misc device: 0x%x", __FUNCTION__, mtml_ipc->header.event_id);
        if (mtml_ipc->header.event_id != IPC_EVENT_GET_DEVICE_INFO)
        {
            sgpu_kfree(mtml_ipc);
            return ret;
        }
        // we can only return 512MB, 1GB, 2GB, 4GB, 8GB and 16GB
        ret = mtml_memory_ioctl(mtml_ipc, mt_gpu_i, mt_sgpu_i);
        if (ret == 0)
        {
            ret = sgpu_copy_to_user(arg_ptr, mtml_ipc, sizeof(ipcMsg_t));
        }
        sgpu_kfree(mtml_ipc);
        return ret;
    }
    else if (nvctl == DEV_GPU_CARD || nvctl == DEV_GPU_RENDER)
    {
        if (arg_cmd != MT_IOCTL_PVR_SRVKM)
            return ret;
        // /dev/dri/card0
        // /dev/dri/renderD128
        srvkm_cmd = sgpu_kmalloc(sizeof(mt_ioctl_srvkm_cmd_t));
        if (srvkm_cmd == NULL)
        {
            sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
            ret = -ENOMEM;
            return ret;
        }
        ret = sgpu_copy_from_user(srvkm_cmd, arg_ptr, sizeof(mt_ioctl_srvkm_cmd_t));
        if (ret != 0)
        {
            sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
            goto post_return;
        }
        if (srvkm_cmd->bridge_id == MT_IOCTL_MM)
        {
            switch (srvkm_cmd->bridge_func_id)
            {
            case MT_IOCTL_MM_PHYSMEMNEWRAMBACKEDPMR: // Memory allocation
                ret = memory_alloc_ioctl(srvkm_cmd, mt_gpu_i, mt_sgpu_i);
                break;
            case MT_IOCTL_MM_PHYSHEAPGETMEMINFO: // Memory info
                ret = memory_info_ioctl(srvkm_cmd, mt_gpu_i, mt_sgpu_i);
                break;
            case MT_IOCTL_MM_PHYSHEAPGETMEMINFOPKD: // Memory info
                ret = memory_info_pkd_ioctl(srvkm_cmd, mt_gpu_i, mt_sgpu_i);
                break;
            case MT_IOCTL_MM_PMRUNREFPMR: // Memory free
                ret = memory_free_ioctl(srvkm_cmd, mt_gpu_i, mt_sgpu_i);
                break;
            }
        }
        else if (srvkm_cmd->bridge_id == MT_IOCTL_RGXCMP &&
                 srvkm_cmd->bridge_func_id == MT_IOCTL_RGXCMP_RGXKICKCDM2)
        {
            rgxcmp_kick = sgpu_kmalloc(srvkm_cmd->in_data_size);
            if (rgxcmp_kick == NULL)
            {
                sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
                ret = -ENOMEM;
                goto post_return;
            }
            ret = sgpu_copy_from_user(rgxcmp_kick, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
            if (ret != 0)
            {
                sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
                goto post_return1;
            }
            fdt = files_fdtable(files);
            tfile = fdt->fd[rgxcmp_kick->hUpdateTimeline];
            if (tfile == NULL)
            {
                sgpu_errorf("sgpu_km[%s] fget failed with fd[%d]", __FUNCTION__, rgxcmp_kick->hUpdateTimeline);
            }
            else
            {
                tfile->f_op = file_op(filp);
            }
            if (mt_gpu_i->policy == sgpu_best_effort)
                goto post_return1;

            sgpu_spin_lock(mt_sgpu_i->lock);
            list_for_each(curr, &mt_sgpu_i->tasks)
            {
                if (((sgpu_task_t *)curr)->tgid == tgid)
                {
                    task = (sgpu_task_t *)curr;
                    break;
                }
            }
            sgpu_spin_unlock(mt_sgpu_i->lock);
            if (task == NULL)
            {
                sgpu_errorf("[%s] failed to find task with tgid: %d", __FUNCTION__, tgid);
                goto post_return1;
            }
            sgpu_spin_lock(mt_sgpu_i->lock);
            list_for_each(curr, &task->rgxcmp_cmds)
            {
                if (((sgpu_task_rgxcmp_cmd_t *)curr)->fake_check_fence == rgxcmp_kick->hCheckFenceFd)
                {
                    rgxcmp_cmd = (sgpu_task_rgxcmp_cmd_t *)curr;
                    break;
                }
            }
            if (rgxcmp_cmd != NULL)
                rgxcmp_cmd->submitted = true;
            else
                sgpu_errorf("[%s] failed to find rgxcmp_cmd by fence[%d]", __FUNCTION__, rgxcmp_kick->hCheckFenceFd);
            sgpu_spin_unlock(mt_sgpu_i->lock);
        }
        else if (srvkm_cmd->bridge_id == MT_IOCTL_DMA &&
                 srvkm_cmd->bridge_func_id == MT_IOCTL_DMA_TRANSFER)
        {
            dma_transfer = sgpu_kmalloc(srvkm_cmd->in_data_size);
            if (dma_transfer == NULL)
            {
                sgpu_errorf("[%s] failed to malloc\n", __FUNCTION__);
                ret = -ENOMEM;
                goto post_return;
            }
            ret = sgpu_copy_from_user(dma_transfer, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
            if (ret != 0)
            {
                sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
                goto post_return1;
            }
            fdt = files_fdtable(files);
            tfile = fdt->fd[dma_transfer->hUpdateTimeline];
            if (tfile == NULL)
            {
                sgpu_errorf("sgpu_km[%s] fget failed with fd[%d]", __FUNCTION__, dma_transfer->hUpdateTimeline);
            }
            else
            {
                tfile->f_op = file_op(filp);
            }
        } else if (srvkm_cmd->bridge_id == MT_IOCTL_RGXTQ2 &&
                 srvkm_cmd->bridge_func_id == MT_IOCTL_RGXTQ2_RGXTDMSUBMITTRANSFER2)
        {
            rgxtq2_transfer = sgpu_kmalloc(srvkm_cmd->in_data_size);
            if (rgxtq2_transfer == NULL)
            {
                sgpu_errorf("[%s] failed to malloc rgxtq2 transfer\n", __FUNCTION__);
                ret = -ENOMEM;
                goto post_return;
            }
            ret = sgpu_copy_from_user(rgxtq2_transfer, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
            if (ret != 0)
            {
                sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
                goto post_return1;
            }
            fdt = files_fdtable(files);
            tfile = fdt->fd[rgxtq2_transfer->hUpdateTimeline];
            if (tfile == NULL)
            {
                sgpu_errorf("sgpu_km[%s] fget failed with fd[%d]", __FUNCTION__, rgxtq2_transfer->hUpdateTimeline);
            }
            else
            {
                tfile->f_op = file_op(filp);
            }
        }
    }

post_return1:
    if (rgxcmp_kick != NULL)
        sgpu_kfree(rgxcmp_kick);
post_return:
    if (srvkm_cmd != NULL)
        sgpu_kfree(srvkm_cmd);
    if (dma_transfer != NULL)
        sgpu_kfree(dma_transfer);
    if (rgxtq2_transfer != NULL)
        sgpu_kfree(rgxtq2_transfer);
    return ret;
}

long memory_info_ioctl(mt_ioctl_srvkm_cmd_t *srvkm_cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i)
{
    mt_ioctl_memory_info_in_t *mem_info_in = NULL;
    mt_ioctl_memory_info_out_t *mem_info_out = NULL;
    mt_ioctl_mem_stats_t mem_stats = {0};
    long ret = 0;

    mem_info_in = sgpu_kmalloc(srvkm_cmd->in_data_size);
    if (mem_info_in == NULL)
    {
        sgpu_errorf("[%s] failed to malloc nvml_get\n", __FUNCTION__);
        ret = -ENOMEM;
        return ret;
    }
    mem_info_out = sgpu_kmalloc(srvkm_cmd->out_data_size);
    if (mem_info_out == NULL)
    {
        sgpu_errorf("[%s] failed to malloc nvml_get\n", __FUNCTION__);
        ret = -ENOMEM;
        goto mem_info_return1;
    }
    ret = sgpu_copy_from_user(mem_info_in, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_info_return2;
    }
    ret = sgpu_copy_from_user(mem_info_out, (void *)(srvkm_cmd->out_data_ptr), srvkm_cmd->out_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_info_return2;
    }
    if (mem_info_out->eError != PVRSRV_OK)
    {
        sgpu_errorf("[%s] memory info ioctl retrun error: %d\n", __FUNCTION__, mem_info_out->eError);
        goto mem_info_return2;
    }
    ret = sgpu_copy_from_user(&mem_stats, mem_info_out->pasapPhysHeapMemStats, sizeof(mem_stats));
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_info_return2;
    }

    sgpu_spin_lock(mt_sgpu_i->lock);
    mem_stats.ui64TotalSize = mt_sgpu_i->total_memory;
    mem_stats.ui64FreeSize = mt_sgpu_i->free_memory;
    sgpu_spin_unlock(mt_sgpu_i->lock);
    ret = sgpu_copy_to_user((void *)(srvkm_cmd->out_data_ptr), mem_info_out, srvkm_cmd->out_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy to user: %ld\n", __FUNCTION__, ret);
    }
    ret = sgpu_copy_to_user((void *)(mem_info_out->pasapPhysHeapMemStats), &mem_stats, sizeof(mem_stats));
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy to user: %ld\n", __FUNCTION__, ret);
    }
mem_info_return2:
    sgpu_kfree(mem_info_out);
mem_info_return1:
    sgpu_kfree(mem_info_in);
    return ret;
}

long memory_info_pkd_ioctl(mt_ioctl_srvkm_cmd_t *srvkm_cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i)
{
    mt_ioctl_memory_info_pkd_in_t *mem_info_in = NULL;
    mt_ioctl_memory_info_pkd_out_t *mem_info_out = NULL;
    mt_ioctl_mem_stats_pkd_t mem_stats = {0};
    long ret = 0;

    mem_info_in = sgpu_kmalloc(srvkm_cmd->in_data_size);
    if (mem_info_in == NULL)
    {
        sgpu_errorf("[%s] failed to malloc nvml_get\n", __FUNCTION__);
        ret = -ENOMEM;
        return ret;
    }
    mem_info_out = sgpu_kmalloc(srvkm_cmd->out_data_size);
    if (mem_info_out == NULL)
    {
        sgpu_errorf("[%s] failed to malloc nvml_get\n", __FUNCTION__);
        ret = -ENOMEM;
        goto mem_info_return1;
    }
    ret = sgpu_copy_from_user(mem_info_in, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_info_return2;
    }
    ret = sgpu_copy_from_user(mem_info_out, (void *)(srvkm_cmd->out_data_ptr), srvkm_cmd->out_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_info_return2;
    }
    if (mem_info_out->eError != PVRSRV_OK)
    {
        sgpu_errorf("[%s] memory info ioctl retrun error: %d\n", __FUNCTION__, mem_info_out->eError);
        goto mem_info_return2;
    }
    ret = sgpu_copy_from_user(&mem_stats, mem_info_out->pasapPhysHeapMemStats, sizeof(mem_stats));
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_info_return2;
    }

    sgpu_spin_lock(mt_sgpu_i->lock);
    mem_stats.ui64TotalSize = mt_sgpu_i->total_memory;
    mem_stats.ui64FreeSize = mt_sgpu_i->free_memory;
    sgpu_spin_unlock(mt_sgpu_i->lock);
    ret = sgpu_copy_to_user((void *)(srvkm_cmd->out_data_ptr), mem_info_out, srvkm_cmd->out_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy to user: %ld\n", __FUNCTION__, ret);
    }
    ret = sgpu_copy_to_user((void *)(mem_info_out->pasapPhysHeapMemStats), &mem_stats, sizeof(mem_stats));
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy to user: %ld\n", __FUNCTION__, ret);
    }
mem_info_return2:
    sgpu_kfree(mem_info_out);
mem_info_return1:
    sgpu_kfree(mem_info_in);
    return ret;
}

long memory_alloc_ioctl(mt_ioctl_srvkm_cmd_t *srvkm_cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i)
{
    long ret = 0;
    struct list_head *curr = NULL;
    int tgid = 0;
    sgpu_task_t *task = NULL;
    sgpu_task_handle_t *handler = NULL;
    mt_ioctl_memory_alloc_in_t *mem_alloc = NULL;
    mt_ioctl_memory_alloc_out_t *mem_ret = NULL;

    // Memory allocation
    mem_alloc = sgpu_kmalloc(srvkm_cmd->in_data_size);
    if (mem_alloc == NULL)
    {
        sgpu_errorf("[%s] failed to malloc mem_alloc\n", __FUNCTION__);
        ret = -ENOMEM;
        return ret;
    }
    mem_ret = sgpu_kmalloc(srvkm_cmd->out_data_size);
    if (mem_ret == NULL)
    {
        sgpu_errorf("[%s] failed to malloc mem_alloc\n", __FUNCTION__);
        ret = -ENOMEM;
        goto mem_alloc_return1;
    }
    ret = sgpu_copy_from_user(mem_alloc, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_alloc_return2;
    }
    ret = sgpu_copy_from_user(mem_ret, (void *)(srvkm_cmd->out_data_ptr), srvkm_cmd->out_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user: %ld\n", __FUNCTION__, ret);
        goto mem_alloc_return2;
    }
    if (mem_ret->eError != PVRSRV_OK)
    {
        goto mem_alloc_return2;
    }
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
    if (task != NULL)
    {
        // add a new task instance into list
        handler = (sgpu_task_handle_t *)sgpu_kmalloc(sizeof(sgpu_task_handle_t));
        if (!handler)
        {
            sgpu_errorf("[%s] failed to malloc handler\n", __FUNCTION__);
            ret = -ENOMEM;
            goto mem_alloc_return3;
        }
        handler->handle = mem_ret->hPMRPtr;
        handler->alloc_memory = mem_alloc->uiSize;
        list_add(&handler->list, &task->items);
        task->alloc_memory += mem_alloc->uiSize;
        mt_sgpu_i->free_memory -= mem_alloc->uiSize;
    }
    else
    {
        sgpu_errorf("[%s][mem_alloc] failed to find task with tgid: %d", __FUNCTION__, tgid);
    }
mem_alloc_return3:
    sgpu_spin_unlock(mt_sgpu_i->lock);
mem_alloc_return2:
    sgpu_kfree(mem_ret);
mem_alloc_return1:
    sgpu_kfree(mem_alloc);
    return ret;
}

long memory_free_ioctl(mt_ioctl_srvkm_cmd_t *srvkm_cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i)
{
    long ret = 0;
    struct list_head *curr = NULL;
    int tgid = 0;
    sgpu_task_t *task = NULL;
    sgpu_task_handle_t *handler = NULL;
    mt_ioctl_memory_free_in_t *mem_free = NULL;
    mt_ioctl_memory_free_out_t *mem_ret = NULL;

    // Memory free
    // sgpu_debugf("[%s]: memory free\n", __FUNCTION__);
    mem_free = sgpu_kmalloc(srvkm_cmd->in_data_size);
    if (mem_free == NULL)
    {
        sgpu_errorf("[%s] failed to malloc mem_free\n", __FUNCTION__);
        ret = -ENOMEM;
        return ret;
    }
    mem_ret = sgpu_kmalloc(srvkm_cmd->out_data_size);
    if (mem_ret == NULL)
    {
        sgpu_errorf("[%s] failed to malloc mem_free\n", __FUNCTION__);
        ret = -ENOMEM;
        goto mem_free_return1;
    }
    ret = sgpu_copy_from_user(mem_free, (void *)(srvkm_cmd->in_data_ptr), srvkm_cmd->in_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user for free: %ld\n", __FUNCTION__, ret);
        goto mem_free_return2;
    }
    ret = sgpu_copy_from_user(mem_ret, (void *)(srvkm_cmd->out_data_ptr), srvkm_cmd->out_data_size);
    if (ret != 0)
    {
        sgpu_errorf("[%s] failed to copy from user for free: %ld\n", __FUNCTION__, ret);
        goto mem_free_return2;
    }
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
        sgpu_errorf("[%s] failed to find task with tgid=%d", __FUNCTION__, tgid);
        goto mem_free_return3;
    }
    list_for_each(curr, &task->items)
    {
        if (((sgpu_task_handle_t *)curr)->handle == mem_free->hPMR)
        {
            handler = (sgpu_task_handle_t *)curr;
            break;
        }
        sgpu_debugf("[mem_free] loop: handle: 0x%x\n", ((sgpu_task_handle_t *)curr)->handle);
    }
    if (handler == NULL)
    {
        sgpu_debugf("[mem_free] failed to find task handler with handle id:0x%x\n", mem_free->hPMR);
        goto mem_free_return3;
    }
    sgpu_debugf("[mem_free] free size=%lld for handler [0x%llx]\n", handler->alloc_memory, mem_free->hPMR);
    if (handler->alloc_memory > 0)
    {
        task->alloc_memory -= handler->alloc_memory;
        mt_sgpu_i->free_memory += handler->alloc_memory;
    }
    list_del(&handler->list);
    sgpu_kfree(handler);
mem_free_return3:
    sgpu_spin_unlock(mt_sgpu_i->lock);
mem_free_return2:
    sgpu_kfree(mem_ret);
mem_free_return1:
    sgpu_kfree(mem_free);
    return ret;
}

int mtml_memory_ioctl(ipcMsg_t *mtml_ipc, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i)
{
    long ret = 0;
    int index = 0;
    if (mt_gpu_i->arch == SUDI)
    {
        boardCapInfo_t *board_cap_info = NULL;
        board_cap_info = sgpu_kmalloc(sizeof(boardCapInfo_t));
        if (board_cap_info == NULL)
        {
            sgpu_errorf("[%s] failed to malloc board_cap_info\n", __FUNCTION__);
            ret = -ENOMEM;
            return ret;
        }

        memcpy(board_cap_info, (void *)mtml_ipc->data, sizeof(boardCapInfo_t));
        index = board_cap_info->ddrCfg_.ddrSize_;
        sgpu_debugf("[%s] get mtml memory ddr size :%d", __FUNCTION__, index);
        if (mt_sgpu_i->total_memory >= (1UL << 30))
            board_cap_info->ddrCfg_.ddrSize_ = mt_sgpu_i->total_memory >> 30;
        else
            board_cap_info->ddrCfg_.ddrSize_ = 129;

        memcpy(mtml_ipc->data, board_cap_info, sizeof(boardCapInfo_t));
        sgpu_kfree(board_cap_info);
    }
    else if (mt_gpu_i->arch == QUYUAN)
    {
        boardCapInfoExtra_t *board_cap_info = NULL;
        deviceInfoMsg_t *device_info = NULL;
        device_info = sgpu_kmalloc(sizeof(deviceInfoMsg_t));
        if (device_info == NULL)
        {
            sgpu_errorf("[%s] failed to malloc device_info\n", __FUNCTION__);
            ret = -ENOMEM;
            return ret;
        }

        memcpy(device_info, (void *)mtml_ipc->data, sizeof(deviceInfoMsg_t));
        board_cap_info = ioremap(device_info->infoBuff_, sizeof(boardCapInfoExtra_t));
        index = board_cap_info->ddrCfg_.ddrSize_;
        sgpu_debugf("[%s] get mtml memory ddr size :%d", __FUNCTION__, index);
        if (mt_sgpu_i->total_memory >= (1UL << 30))
            board_cap_info->ddrCfg_.ddrSize_ = mt_sgpu_i->total_memory >> 30;
        else
            board_cap_info->ddrCfg_.ddrSize_ = 129;

        memcpy(mtml_ipc->data, device_info, sizeof(deviceInfoMsg_t));
        iounmap(board_cap_info);
        sgpu_kfree(device_info);
    }

    switch (index)
    {
    case 1:
        mt_gpu_i->mem_total_size = 1UL << 30;
        break;
    case 2:
        mt_gpu_i->mem_total_size = 2UL << 30;
        break;
    case 4:
        mt_gpu_i->mem_total_size = 4UL << 30;
        break;
    case 8:
        mt_gpu_i->mem_total_size = 8UL << 30;
        break;
    case 16:
        mt_gpu_i->mem_total_size = 16UL << 30;
        break;
    }

    return ret;
}
