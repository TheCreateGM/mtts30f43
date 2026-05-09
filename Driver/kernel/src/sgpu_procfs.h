/*
 *  Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */
#pragma once
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/seq_file.h>

#include "sgpu.h"

static struct proc_dir_entry *proc_root;
static struct proc_dir_entry *proc_insts[MAX_GPU];
static struct proc_dir_entry *node_cores;
static struct proc_dir_entry *node_inst_ctl;
static struct proc_dir_entry *node_major;
static struct proc_dir_entry *node_major_ctl;
static struct proc_dir_entry *node_version;
extern struct mt_global_s *mt_global;
extern int total_gpu_num;
extern int gpu_ids[MAX_GPU];
extern int gpu_ids_num;

char *sgpu_km_get_cores(void);
int sgpu_km_set_cores(char *cores);
int inst_get_minor(void *priv);
uint64_t inst_get_total_mem(void *priv);
int inst_set_total_mem(void *priv, uint64_t total_mem);
void *inst_get_node(char *name, int i);
void inst_set_name(void *priv, char *buf);
int group_set_policy(void *priv, int policy);
int group_get_policy(void *priv);
int group_set_time_slice(void *priv, int time_slice);
int group_get_time_slice(void *priv);
int group_set_overcommit_ratio(void *priv, int overcommit_ratio);
int group_get_overcommit_ratio(void *priv);
int group_get_max_inst(void *priv);
int group_set_max_inst(void *priv, int max_inst);
struct sgpu_meminfo *inst_get_meminfo(void *priv);
extern int sgpu_proc_ioctl(int code, void *argi, void *argo, void *data);
int inst_set_weight(void *priv, int weight);
int inst_get_weight(void *priv);
extern int sgpu_major;
extern int sgpu_ctl_major;