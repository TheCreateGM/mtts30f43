/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/sync_file.h>
#include <uapi/linux/sync_file.h>

#include "sgpu.h"

const char *dma_fence_get_name(struct dma_fence *fence)
{
    return "sgpu-fence";
}

const struct dma_fence_ops fence_ops = {
    .get_driver_name = dma_fence_get_name,
    .get_timeline_name = dma_fence_get_name,
};

int create_fence(int *fd_to_resolv, struct dma_fence *fence, spinlock_t *fence_lock)
{
    struct sync_file *sync_file = NULL;
    struct dma_fence_array *array = NULL;
    struct dma_fence *dep_fence = NULL;
    struct dma_fence **fences;
    int fd, num = 1, i = 0;

    dma_fence_init(fence, &fence_ops, fence_lock, 0, 0);
    fd = get_unused_fd_flags(0);

    if (*fd_to_resolv < 0)
    {
        *fd_to_resolv = fd;
        sync_file = sync_file_create(fence);
        fd_install(fd, sync_file->file);
        return 0;
    }

    dep_fence = sync_file_get_fence(*fd_to_resolv);
    if (dma_fence_is_array(dep_fence))
    {
        array = to_dma_fence_array(dep_fence);
        num = array->num_fences + 1;
    }
    else
        num = 2;
    fences = kcalloc(num, sizeof(*fences), GFP_KERNEL);
    for (i = 0; i < num - 1; i++)
    {
        if (array != NULL)
            fences[i] = array->fences[i];
        else
            fences[i] = dep_fence;
    }
    fences[i] = fence;
    array = dma_fence_array_create(num, fences, dma_fence_context_alloc(1), 1, false);
    sync_file = sync_file_create(&array->base);
    fd_install(fd, sync_file->file);
    *fd_to_resolv = fd;
    return 0;
}
