/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 */

#include "sgpu_linux.h"

extern int sgpu_ctl_major;

bool sgpu_kthread_should_stop(void)
{
    return kthread_should_stop();
}

void *sgpu_kthread_run(SGPU_THREAD_FN sched_thread_fn, void *data, const char *name)
{
    return (void *)kthread_run((SGPU_THREAD_FN)sched_thread_fn, data, name);
}

void *sgpu_memcpy(void *dest, const void *src, unsigned long n)
{
    return memcpy(dest, src, n);
}

void sgpu_memcpy_fromio(void *to, const volatile void *from, long count)
{
    memcpy_fromio(to, from, count);
}

void *sgpu_memset(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

void sgpu_kthread_stop(struct task_struct *sch)
{
    kthread_stop(sch);
}

void sgpu_spin_lock_init(spinlock_t *lock)
{
    spin_lock_init(lock);
}

void sgpu_spin_lock(spinlock_t *lock)
{
    spin_lock(lock);
}

void sgpu_spin_unlock(spinlock_t *lock)
{
    spin_unlock(lock);
}

void *sgpu_kmalloc(int len)
{
    return kmalloc(len, GFP_KERNEL);
}

void *sgpu_kmalloc_array(int len, int size)
{
    return kmalloc_array(len, size, GFP_KERNEL | __GFP_ZERO);
}

void sgpu_kfree(const void *objp)
{
    kfree(objp);
}

unsigned long sgpu_copy_from_user(void *to, const void __user *from, unsigned long n)
{
    return copy_from_user(to, from, n);
}

unsigned long sgpu_copy_to_user(void __user *to, const void *from, unsigned long n)
{
    return copy_to_user(to, from, n);
}

unsigned int sgpu_get_major(unsigned rdev)
{
    return MAJOR(rdev);
}

unsigned int sgpu_get_minor(unsigned rdev)
{
    return MINOR(rdev);
}

struct file *sgpu_filp_open(const char *filename, int flags, unsigned short mode)
{
    return filp_open(filename, flags, mode);
}

void sgpu_ireadcount_dec(struct inode *i)
{
    i_readcount_dec(i);
}

void sgpu_ireadcount_inc(struct inode *i)
{
    i_readcount_inc(i);
}

void sgpu_init_list_head(struct list_head *list)
{
    INIT_LIST_HEAD(list);
}

int DEV_TYPE(int major, int minor)
{
    if (major == sgpu_ctl_major)
        return DEV_GPU_CTL;
    if (minor > 0x7f)
        return DEV_GPU_RENDER;
    return DEV_GPU_CARD;
}

int DEV_GPU_ID(int minor)
{
    if (minor > 0x7f)
        minor = minor - 0x7f;
    return minor >> 4;
}

int DEV_SGPU_ID(int minor)
{
    return minor & 0xf;
}