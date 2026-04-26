/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 */

#pragma once
#include <linux/cdev.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/dmi.h>
#include <linux/version.h>
#include <asm/io.h>

#include "sgpu.h"

typedef int (*SGPU_THREAD_FN)(void *data);

struct file *sgpu_filp_open(const char *filename, int flags, unsigned short mode);
void sgpu_ireadcount_dec(struct inode *i);
void sgpu_ireadcount_inc(struct inode *i);
void *sgpu_kmalloc(int len);
void *sgpu_kmalloc_array(int len, int size);
void sgpu_kfree(const void *objp);
unsigned int sgpu_get_major(unsigned rdev);
unsigned int sgpu_get_minor(unsigned rdev);
unsigned long sgpu_copy_from_user(void *to, const void *from, unsigned long n);
unsigned long sgpu_copy_to_user(void *to, const void *from, unsigned long n);
void *sgpu_memcpy(void *dest, const void *src, size_t count);
void sgpu_spin_lock_init(spinlock_t *lock);
void sgpu_spin_lock(spinlock_t *lock);
void sgpu_spin_unlock(spinlock_t *lock);
void sgpu_init_list_head(struct list_head *list);

// for core schedule
void *sgpu_kthread_run(SGPU_THREAD_FN sched_thread_fn, void *data, const char *name);
void sgpu_kthread_stop(struct task_struct *sch);
void init_list_head(struct list_head *list);
bool sgpu_kthread_should_stop(void);

int DEV_SGPU_ID(int minor);
int DEV_GPU_ID(int minor);
int DEV_TYPE(int major, int minor);
