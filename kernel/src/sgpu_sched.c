/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <drm/drm.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include "sgpu.h"

int kickCommand(sgpu_task_rgxcmp_cmd_t *cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i);

int core_rgxcmp_sched(void *ptr)
{
    mt_gpu_t *mt_gpu_i;
    mt_sgpu_t *mt_sgpu_i;
    struct list_head *curr = NULL;
    sgpu_task_t *task = NULL;
    int next_sgpu_id = 0;
    bool kthread_should_stop = false;
    sgpu_task_rgxcmp_cmd_t *rgxcmp_cmd = NULL, *curr_cmd = NULL;
    int ret = 0, weight = 0;

    // init mt_gpu_i
    mt_gpu_i = (mt_gpu_t *)ptr;
    sgpu_debugf("[%s] start, time slice %d~%d\n",
                __FUNCTION__, mt_gpu_i->time_slice_minimum,
                mt_gpu_i->time_slice_maximum);
    mt_sgpu_i = &(mt_gpu_i->instances[mt_gpu_i->curr_sgpu_id]);

run:
    // sleep time slice
    usleep_range(mt_gpu_i->time_slice_minimum, mt_gpu_i->time_slice_maximum);
    kthread_should_stop = sgpu_kthread_should_stop();
    if (kthread_should_stop)
        goto ret1;

    // get next sgpu id
    next_sgpu_id = mt_gpu_i->curr_sgpu_id;
    while (true)
    {
        if (!is_gpu_used(mt_gpu_i))
            goto ret1;
        if (weight <= 0) {
            next_sgpu_id = (next_sgpu_id + 1) % mt_gpu_i->max_inst;
            mt_sgpu_i = &mt_gpu_i->instances[next_sgpu_id];
            weight = mt_sgpu_i->weight;
        }
        // get next sgpu which is running
        if (is_inst_used(&(mt_gpu_i->instances[next_sgpu_id])))
            break;
        else if (mt_gpu_i->policy == sgpu_fair_preemption) {
            weight = 0;
            continue;
        }

        if (mt_gpu_i->policy == sgpu_fair_fixed)
            break;
    }
    //sgpu_debugf("[%s] sched for sgpu id=[%d/%d]\n", __FUNCTION__, mt_gpu_i->gpu_id, next_sgpu_id);

    // switch
    if (mt_gpu_i->curr_sgpu_id != next_sgpu_id)
    {
        // sgpu_debugf("switching ctx from %d to %d\n",mt_gpu_i->curr_ctx_id,next_sgpu_id);
        mt_gpu_i->curr_sgpu_id = next_sgpu_id;
        mt_sgpu_i = &(mt_gpu_i->instances[mt_gpu_i->curr_sgpu_id]);
    }

    sgpu_spin_lock(mt_sgpu_i->lock);
    weight --;
    list_for_each(curr, &mt_sgpu_i->tasks)
    {
        task = (sgpu_task_t *)curr;
        //sgpu_debugf("[%s] current sched task = %d for instance[%d/%d] with weight=%d\n", __FUNCTION__, task->tgid, mt_gpu_i->gpu_id, mt_sgpu_i->minor, weight);

        // kick scheduled items
        list_for_each_entry_safe(rgxcmp_cmd, curr_cmd, &task->rgxcmp_cmds, list)
        {
            if (rgxcmp_cmd == NULL || rgxcmp_cmd->submitted == false)
                continue;
            ret = kickCommand(rgxcmp_cmd, mt_gpu_i, mt_sgpu_i);
            if (ret != 0)
                sgpu_errorf("[%s] failed to kick command: %d\n", __FUNCTION__, ret);
            else
            {
                list_del(&rgxcmp_cmd->list);
                put_spin_lock(rgxcmp_cmd->fence_lock);
                sgpu_kfree(rgxcmp_cmd);
            }
        }
    }
    sgpu_spin_unlock(mt_sgpu_i->lock);
    goto run;

ret1:
    mt_gpu_i->task_struct = NULL;
    sgpu_debugf("core_rgxcmp_sched end\n");
    return 0;
}

// kick command by sending ioctl commands
int kickCommand(sgpu_task_rgxcmp_cmd_t *cmd, mt_gpu_t *mt_gpu_i, mt_sgpu_t *mt_sgpu_i)
{
    dma_fence_signal(cmd->fake_fence);
    sgpu_debugf("[%s] send signal", __FUNCTION__);
    return 0;
}
