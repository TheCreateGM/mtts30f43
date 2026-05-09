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

#include "sgpu_linux.h"

typedef enum fops_types fops_types_e;
enum fops_types
{
    sgpu_poll,
    sgpu_unlocked_ioctl,
    sgpu_compat_ioctl,
    sgpu_mmap,
    sgpu_open,
    sgpu_release,
};

typedef struct
{
    uint32_t domain;    /* PCI domain number   */
    uint32_t bus;       /* PCI bus number      */
    uint8_t slot;       /* PCI slot number     */
    uint8_t function;   /* PCI function number */
    uint16_t vendor_id; /* PCI vendor ID       */
    uint16_t device_id; /* PCI device ID       */
} nv_pci_info_t;

void sgpu_errorf(const char *printf_format, ...);
void sgpu_printf(const char *printf_format, ...);
void sgpu_debugf(const char *printf_format, ...);
void *get_spin_lock(void);
void put_spin_lock(void *lock);
struct file *filp_open_check(const char *filename, int flags, unsigned short mode);
struct inode *get_file_inode(struct file *filp);
const struct file_operations *file_op(struct file *filp);
struct inode *get_inode(struct file *filp);
unsigned int get_rdev(struct inode *nv_inode);
unsigned int get_file_major(struct file *filp);
unsigned int get_file_minor(struct file *filp);
struct kobject *cdev_module_get(struct inode *inode);
void cdev_module_put(struct inode *inode);
void *sgpu_get_fops(void *f, int type);
void set_filp(struct file *filp, struct inode *inode);
uint16_t get_device_id(struct file *filp);
int get_tgid(void);
char *get_error(int);
void find_diff(int* old_arr, int old_arr_size, int* new_arr, int new_arr_size, int*** diff_arr, int** diff_sizes);