/*
 *  Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#include "sgpu_procfs.h"

int is_gpu_opened(mt_gpu_t * mt_gpu_it);
void sgpu_finalize_gpu(int gpu_id);
mt_gpu_t *sgpu_initialize_gpu(int gpu_id);

/*
 * Compatibility wrapper for kernel 5.6+ where proc_create_data
 * expects struct proc_ops * instead of struct file_operations *
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)

#include <linux/proc_fs.h>

/*
 * In kernel 5.6+, proc_create_data takes const struct proc_ops *.
 * We use proc_create_single_data for read-only seq_file nodes,
 * and proc_create for writable nodes.
 * 
 * Since all our procfs nodes use seq_file (single_open), we can use
 * proc_create_data directly with proc_ops. The trick is that proc_ops
 * must be statically allocated. We achieve this by using a per-call
 * static approach via a helper that allocates proc_ops with kmalloc
 * and stores it in the proc_dir_entry private data chain.
 *
 * Simpler approach: just use proc_create_data with a heap-allocated
 * proc_ops that lives as long as the proc entry.
 */
struct sgpu_proc_ops_wrapper {
    struct proc_ops ops;
    /* ops must be first for pointer casting */
};

struct proc_dir_entry *sgpu_compat_proc_create_data(
    const char *name, umode_t mode, struct proc_dir_entry *parent,
    const struct file_operations *fops, void *data)
{
    struct sgpu_proc_ops_wrapper *wrapper;
    struct proc_dir_entry *entry;

    if (!fops)
        return proc_create_data(name, mode, parent, NULL, data);

    wrapper = kzalloc(sizeof(*wrapper), GFP_KERNEL);
    if (!wrapper)
        return NULL;

    wrapper->ops.proc_open    = fops->open;
    wrapper->ops.proc_read    = fops->read;
    wrapper->ops.proc_write   = fops->write;
    wrapper->ops.proc_lseek   = fops->llseek;
    wrapper->ops.proc_release = fops->release;
    wrapper->ops.proc_ioctl   = fops->unlocked_ioctl;
    wrapper->ops.proc_poll    = fops->poll;
#ifdef CONFIG_COMPAT
    wrapper->ops.proc_compat_ioctl = fops->compat_ioctl;
#endif

    entry = proc_create_data(name, mode, parent, &wrapper->ops, data);
    if (!entry)
        kfree(wrapper);
    /* wrapper is intentionally leaked - it lives as long as the proc entry */
    return entry;
}

/* Override proc_create_data to use our wrapper */
#undef proc_create_data
#define proc_create_data sgpu_compat_proc_create_data

/* Compatibility for no_llseek -> noop_llseek */
#ifndef no_llseek
#define no_llseek noop_llseek
#endif

/* Compatibility wrapper for PDE_DATA */
/* In old kernels, PDE_DATA returned long, in new kernels it's a function returning void* */
static inline void *sgpu_pde_data(const struct inode *inode)
{
    /* For old kernels, PDE_DATA was a macro returning long */
    /* For new kernels, pde_data() returns void* */
    return (void *)pde_data(inode);
}
#define PDE_DATA sgpu_pde_data

#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0) */


static int read_version(struct seq_file *seqf, void *data)
{
    seq_printf(seqf, "%d.%d.%d\n", SGPU_MAJOR_VERSION, SGPU_MINOR_VERSION,
               SGPU_BUILD_VERSION);
    return 0;
}

static int sgpu_version_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);
    return single_open(filp, read_version, data);
}

static const struct file_operations sgpu_version_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_version_node_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_id_node(struct seq_file *seqf, void *data)
{
    int minor = inst_get_minor(seqf->private);

    seq_printf(seqf, "%d\n", minor);
    return 0;
}

static int sgpu_id_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_id_node, data);
}

static const struct file_operations sgpu_id_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_id_node_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_meminfo_node(struct seq_file *seqf, void *data)
{
    struct sgpu_meminfo *info = inst_get_meminfo(seqf->private);
    struct sgpu_meminfo_entry *entry = NULL;
    int i = 0;

    if (!info)
    {
        sgpu_errorf("invalid info\n");
        return -EFAULT;
    }

    seq_printf(seqf, "Free: %lld\n", info->free_size);
    for (i = 0; i < info->num; i++)
    {
        entry = &info->entries[i];

        seq_printf(seqf, "PID: %d Mem: %lld\n", entry->pid,
                   entry->mem_size);
    }

    if (info->num > 0)
    {
        sgpu_kfree(info->entries);
    }
    sgpu_kfree(info);
    return 0;
}

static int sgpu_meminfo_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_meminfo_node, data);
}

static const struct file_operations sgpu_meminfo_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_meminfo_node_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_memsize_node(struct seq_file *seqf, void *data)
{
    uint64_t total_mem = inst_get_total_mem(seqf->private);

    seq_printf(seqf, "%lld\n", total_mem);
    return 0;
}

static int sgpu_memsize_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_memsize_node, data);
}

static ssize_t sgpu_memsize_node_write(struct file *filp,
                                       const char __user *buf, size_t count, loff_t *ppos)
{
    void *priv = NULL;
    char tmp[16];
    int ret = 0;
    uint64_t size = 0;

    if (!filp || !file_inode(filp) || !buf)
    {
        sgpu_errorf("invalid filp\n");
        return -EINVAL;
    }

    priv = PDE_DATA(file_inode(filp));
    if (!priv)
    {
        sgpu_errorf("invalid priv\n");
        return -EINVAL;
    }

    if (count > 15)
    {
        sgpu_errorf("invalid memsize buff size %ld\n", count);
        return -EINVAL;
    }

    ret = copy_from_user(tmp, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy from user %d\n", ret);
        return ret;
    }

    tmp[count] = '\0';
    ret = sscanf(tmp, "%lld\n", &size);
    if (ret != 1)
    {
        sgpu_errorf("failed to parse string %s\n", tmp);
        return -EINVAL;
    }
    // make sure the memory size is 512MB, 1GB, 2GB, 4GB and so on
    if (size > (16UL << 30))
    {
        sgpu_errorf("cannot set memory size over 16GB");
        return -1;
    }
    else if (size > (8UL << 30))
        size = 16UL << 30;
    else if (size > (4UL << 30))
        size = 8UL << 30;
    else if (size > (2UL << 30))
        size = 4UL << 30;
    else if (size > (1UL << 30))
        size = 2UL << 30;
    else if (size > (1UL << 29))
        size = 1UL << 30;
    else
        size = 512UL << 20;
    ret = inst_set_total_mem(priv, size);
    if (ret != 0)
    {
        sgpu_errorf("failed to set total memory = %d\n", ret);
        return ret;
    }
    return count;
}

static const struct file_operations sgpu_memsize_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_memsize_node_open,
    .read = seq_read,
    .write = sgpu_memsize_node_write,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_weight_node(struct seq_file *seqf, void *data)
{
    int weight = inst_get_weight(seqf->private);

    seq_printf(seqf, "%d\n", weight);
    return 0;
}

static int sgpu_weight_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_weight_node, data);
}

static ssize_t sgpu_weight_node_write(struct file *filp,
                                      const char __user *buf, size_t count, loff_t *ppos)
{
    void *priv;
    char tmp[6];
    int ret;
    int weight;

    if (!filp || !file_inode(filp) || !buf)
        return -EINVAL;

    priv = PDE_DATA(file_inode(filp));
    if (!priv)
        return -EINVAL;

    if (count > 5)
    {
        sgpu_errorf("invalid weight buff size %ld\n", count);
        return -EINVAL;
    }

    ret = copy_from_user(tmp, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy from user %d\n", ret);
        return ret;
    }

    tmp[count] = '\0';
    ret = sscanf(tmp, "%d\n", &weight);
    if (ret != 1)
    {
        sgpu_errorf("failed to parse string %s\n", tmp);
        return -EINVAL;
    }
    ret = inst_set_weight(priv, weight);
    return count;
}

static const struct file_operations sgpu_weight_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_weight_node_open,
    .read = seq_read,
    .write = sgpu_weight_node_write,
    .llseek = seq_lseek,
    .release = single_release,
};

int create_new_node(char *container_id, int gpu_id)
{
    int i, j, ret = 0;
    char *tmp = NULL;
    struct proc_dir_entry *inst_root = NULL, *inst = NULL;
    void *priv = NULL;
    bool is_valid_num = false;
    for (i = 0; i < total_gpu_num; i++)
    {
        j = i;
        if (gpu_ids_num >= 1) {
            j = gpu_ids[i];
        }

        if (j == gpu_id) {
            is_valid_num = true;
            break;
        }
    }

    if (!is_valid_num)
    {
        sgpu_errorf("invalid new node num %d\n", gpu_id);
        return -EINVAL;
    }

    if (!container_id || !proc_insts[gpu_id])
    {
        sgpu_errorf("create new node null ptr\n");
        return -EINVAL;
    }

    priv = inst_get_node(NULL, gpu_id);

    if (!priv)
    {
        sgpu_errorf("no instance available\n");
        return -ENOSPC;
    }

    tmp = sgpu_kmalloc(NODE_NAME_LEN + 1);
    if (!tmp)
    {
        sgpu_errorf("failed to allocate memory for node name, length %d\n", NODE_NAME_LEN);
        return -ENOMEM;
    }

    strcpy(tmp, container_id);
    tmp[NODE_NAME_LEN] = '\0';
    inst_set_name(priv, tmp);
    inst_root = proc_mkdir(tmp, proc_insts[gpu_id]);
    if (!inst_root)
    {
        sgpu_errorf("failed to create node %s\n", tmp);
        ret = -EFAULT;
        goto err_free;
    }

    inst = proc_create_data("id", 0444, inst_root,
                            &sgpu_id_node_fops, priv);
    if (!inst)
    {
        sgpu_errorf("failed to create node \"id\"\n");
        ret = -EFAULT;
        goto err_procfs;
    }

    inst = proc_create_data("meminfo", 0444, inst_root,
                            &sgpu_meminfo_node_fops, priv);
    if (!inst)
    {
        sgpu_errorf("failed to create node \"meminfo\"\n");
        ret = -EFAULT;
        goto err_procfs;
    }

    inst = proc_create_data("memsize", 0666, inst_root,
                            &sgpu_memsize_node_fops, priv);
    if (!inst)
    {
        sgpu_errorf("failed to create node \"memsize\"\n");
        ret = -EFAULT;
        goto err_procfs;
    }

    inst = proc_create_data("weight", 0666, inst_root,
                            &sgpu_weight_node_fops, priv);
    if (!inst)
    {
        sgpu_errorf("failed to create node \"weight\"\n");
        goto err_procfs;
    }

    sgpu_printf("create sgpu minor id %d\n", inst_get_minor(priv));

    return 0;

err_procfs:
    proc_remove(inst_root);
err_free:
    inst_set_name(priv, NULL);

    return ret;
}

int destroy_node(char *name)
{
    int ret = 0, i = 0;
    int gpu_id = 0;
    void *priv = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;
    sgpu_task_t *task = NULL, *curr = NULL;

    sgpu_debugf("[%s] start", __FUNCTION__);
    if (!name)
    {
        sgpu_errorf("destroy node null ptr\n");
        return -EINVAL;
    }

    for (i = 0; i < total_gpu_num; i++)
    {
        gpu_id = i;
        if (gpu_ids_num >= 1) {
            gpu_id = gpu_ids[i];
        }

        priv = inst_get_node(name, gpu_id);
        if (priv)
        {
            break;
        }
    }
    if (!priv)
    {
        sgpu_errorf("cannot find sGPU instance\n");
        return -EINVAL;
    }

    inst_set_name(priv, NULL);
    mt_sgpu_i = (mt_sgpu_t *)priv;
    sgpu_spin_lock(mt_sgpu_i->lock);
    list_for_each_entry_safe(task, curr, &mt_sgpu_i->tasks, list)
    {
        clean_sgpu_task(task, mt_sgpu_i);
    }
    sgpu_spin_unlock(mt_sgpu_i->lock);
    ret = remove_proc_subtree(name, proc_insts[gpu_id]);
    return ret;
}

static int read_cores_node(struct seq_file *seqf, void *data)
{
    seq_printf(seqf, "%s\n", sgpu_km_get_cores());
    return 0;
}

static int sgpu_cores_node_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, read_cores_node, NULL);
}

static ssize_t sgpu_cores_node_write(struct file *filp, const char __user *buf,
                                   size_t count, loff_t *ppos)
{
    char *cores = NULL;
    int ret = 0;

    if (!buf)
    {
        sgpu_errorf("invalid input buf %p count %ld\n", buf, count);
        return -EINVAL;
    }

    cores = sgpu_kmalloc(count + 1);
    if (!cores)
    {
        sgpu_errorf("failed to allocate buf len %ld\n", count);
        return -ENOMEM;
    }

    ret = copy_from_user(cores, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy data %d\n", ret);
        goto out_free;
    }

    cores[count] = '\0';
    ret = sgpu_km_set_cores(cores);
    if (ret)
    {
        sgpu_errorf("failed to set cores %s\n", cores);
        goto out_free;
    }
    
    ret = count;
out_free:
    kfree(cores);
    return ret;
}

static long sgpu_cores_node_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return 0;
}

static const struct file_operations sgpu_cores_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_cores_node_open,
    .read = seq_read,
    .write = sgpu_cores_node_write,
    .unlocked_ioctl = sgpu_cores_node_ioctl,
    .llseek = no_llseek,
};


static ssize_t sgpu_inst_ctl_read(struct file *filp, char __user *buf,
                                  size_t count, loff_t *ppos)
{
    return -EBADF;
}

static ssize_t sgpu_inst_ctl_write(struct file *filp, const char __user *buf,
                                   size_t count, loff_t *ppos)
{
    char *node_name = NULL;
    int ret = 0;

    if (!buf || (count != NODE_NAME_LEN + 1))
    {
        sgpu_errorf("invalid input buf %p count %ld\n", buf, count);
        return -EINVAL;
    }

    node_name = sgpu_kmalloc(count + 1);
    if (!node_name)
    {
        sgpu_errorf("failed to allocate buf len %ld\n", count);
        return -ENOMEM;
    }

    ret = copy_from_user(node_name, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy data %d\n", ret);
        goto out_free;
    }

    node_name[count] = '\0';
    if (node_name[0] == '-')
    {
        ret = destroy_node(node_name + 1);
        if (ret)
        {
            sgpu_errorf("failed to destroy node %s\n", node_name + 1);
            goto out_free;
        }
    }
    else
    {
        ret = create_new_node(node_name + 1, node_name[0] - '0');
        if (ret)
        {
            sgpu_errorf("failed to create new node %s\n", node_name);
            goto out_free;
        }
    }

    ret = count;
out_free:
    kfree(node_name);
    return ret;
}

static long sgpu_inst_ctl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return 0;
}

static const struct file_operations sgpu_inst_ctl_fops = {
    .owner = THIS_MODULE,
    .open = nonseekable_open,
    .read = sgpu_inst_ctl_read,
    .write = sgpu_inst_ctl_write,
    .unlocked_ioctl = sgpu_inst_ctl_ioctl,
    .llseek = no_llseek,
};

static int read_major_node(struct seq_file *seqf, void *data)
{
    seq_printf(seqf, "%d\n", sgpu_major);
    return 0;
}

static int sgpu_major_node_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, read_major_node, NULL);
}

static const struct file_operations sgpu_major_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_major_node_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_major_node_ctl(struct seq_file *seqf, void *data)
{
    seq_printf(seqf, "%d\n", sgpu_ctl_major);
    return 0;
}

static int sgpu_major_node_ctl_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, read_major_node_ctl, NULL);
}

static const struct file_operations sgpu_major_node_ctl_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_major_node_ctl_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_max_inst_node(struct seq_file *seqf, void *data)
{
    seq_printf(seqf, "%d\n", group_get_max_inst(seqf->private));
    return 0;
}

static int sgpu_max_inst_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);
    return single_open(filp, read_max_inst_node, data);
}

static ssize_t sgpu_max_inst_node_write(struct file *filp,
                                        const char __user *buf, size_t count, loff_t *ppos)
{
    void *priv;
    char tmp[4];
    int ret = 0;
    int max_inst = 0;

    if (!filp || !file_inode(filp) || !buf)
    {
        sgpu_errorf("[%s] invalid filp\n", __FUNCTION__);
        return -EINVAL;
    }

    priv = PDE_DATA(file_inode(filp));
    if (!priv)
    {
        return -EINVAL;
    }

    if (count > 3)
    {
        sgpu_errorf("[%s] invalid max_inst buff size %ld\n", __FUNCTION__, count);
        return -EINVAL;
    }

    ret = copy_from_user(tmp, buf, count);
    if (ret)
    {
        sgpu_errorf("[%s] failed to copy from user %d\n", __FUNCTION__, ret);
        return ret;
    }

    tmp[count] = '\0';
    ret = sscanf(tmp, "%d\n", &max_inst);
    if (ret != 1)
    {
        sgpu_errorf("[%s] failed to parse string %s\n", __FUNCTION__, tmp);
        return -EINVAL;
    }
    ret = group_set_max_inst(priv, max_inst);
    if (ret != 0)
    {
        return ret;
    }

    return count;
}

static const struct file_operations sgpu_max_inst_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_max_inst_node_open,
    .read = seq_read,
    .write = sgpu_max_inst_node_write,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_policy_node(struct seq_file *seqf, void *data)
{
    int policy = group_get_policy(seqf->private);

    seq_printf(seqf, "%d\n", policy);
    return 0;
}

static int sgpu_policy_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_policy_node, data);
}

static ssize_t sgpu_policy_node_write(struct file *filp,
                                      const char __user *buf, size_t count, loff_t *ppos)
{
    void *priv;
    char tmp[3];
    int ret;
    int policy;

    if (!filp || !file_inode(filp) || !buf)
    {
        return -EINVAL;
    }

    priv = PDE_DATA(file_inode(filp));
    if (!priv)
    {
        return -EINVAL;
    }

    if (count > 2)
    {
        sgpu_errorf("invalid policy buff size %ld\n", count);
        return -EINVAL;
    }

    ret = copy_from_user(tmp, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy from user %d\n", ret);
        return ret;
    }

    tmp[count] = '\0';
    ret = sscanf(tmp, "%d\n", &policy);
    if (ret != 1)
    {
        sgpu_errorf("failed to parse string %s\n", tmp);
        return -EINVAL;
    }
    ret = group_set_policy(priv, policy);
    if (ret != 0)
        return ret;
    return count;
}

static const struct file_operations sgpu_policy_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_policy_node_open,
    .read = seq_read,
    .write = sgpu_policy_node_write,
    .llseek = seq_lseek,
    .release = single_release,
};

static int read_time_slice_node(struct seq_file *seqf, void *data)
{
    int time_slice = group_get_time_slice(seqf->private);

    seq_printf(seqf, "%d\n", time_slice);
    return 0;
}

static int sgpu_time_slice_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_time_slice_node, data);
}

static ssize_t sgpu_time_slice_node_write(struct file *filp,
                                          const char __user *buf, size_t count, loff_t *ppos)
{
    void *priv;
    char tmp[4];
    int ret;
    int time_slice;

    if (!filp || !file_inode(filp) || !buf)
    {
        return -EINVAL;
    }

    priv = PDE_DATA(file_inode(filp));
    if (!priv)
    {
        return -EINVAL;
    }

    if (count > 3)
    {
        sgpu_errorf("invalid time_slice buff size %ld\n", count);
        return -EINVAL;
    }

    ret = copy_from_user(tmp, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy from user %d\n", ret);
        return ret;
    }

    tmp[count] = '\0';
    ret = sscanf(tmp, "%d\n", &time_slice);
    if (ret != 1)
    {
        sgpu_errorf("failed to parse string %s\n", tmp);
        return -EINVAL;
    }
    ret = group_set_time_slice(priv, time_slice);
    if (ret != 0)
        return ret;
    return count;
}

static const struct file_operations sgpu_time_slice_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_time_slice_node_open,
    .read = seq_read,
    .write = sgpu_time_slice_node_write,
    .llseek = seq_lseek,
    .release = single_release,
};


static int read_overcommit_ratio_node(struct seq_file *seqf, void *data)
{
    int overcommit_ratio = group_get_overcommit_ratio(seqf->private);

    seq_printf(seqf, "%d\n", overcommit_ratio);
    return 0;
}

static int sgpu_overcommit_ratio_node_open(struct inode *inode, struct file *filp)
{
    void *data = PDE_DATA(inode);

    if (!data)
    {
        sgpu_errorf("cannot get data from inode %p\n", inode);
        return -EINVAL;
    }

    return single_open(filp, read_overcommit_ratio_node, data);
}

static ssize_t sgpu_overcommit_ratio_node_write(struct file *filp,
                                          const char __user *buf, size_t count, loff_t *ppos)
{
    void *priv;
    char tmp[5];
    int ret;
    int overcommit_ratio;

    if (!filp || !file_inode(filp) || !buf)
    {
        return -EINVAL;
    }

    priv = PDE_DATA(file_inode(filp));
    if (!priv)
    {
        return -EINVAL;
    }

    if (count > 4)
    {
        sgpu_errorf("invalid overcommit_ratio buff size %ld\n", count);
        return -EINVAL;
    }

    ret = copy_from_user(tmp, buf, count);
    if (ret)
    {
        sgpu_errorf("failed to copy from user %d\n", ret);
        return ret;
    }

    tmp[count] = '\0';
    ret = sscanf(tmp, "%d\n", &overcommit_ratio);
    if (ret != 1)
    {
        sgpu_errorf("failed to parse string %s\n", tmp);
        return -EINVAL;
    }
    ret = group_set_overcommit_ratio(priv, overcommit_ratio);
    if (ret != 0)
        return ret;
    return count;
}

static const struct file_operations sgpu_overcommit_ratio_node_fops = {
    .owner = THIS_MODULE,
    .open = sgpu_overcommit_ratio_node_open,
    .read = seq_read,
    .write = sgpu_overcommit_ratio_node_write,
    .llseek = seq_lseek,
    .release = single_release,
};

int sgpu_km_procfs_gpu_init(int gpu_id)
{
    int ret = 0;
    char inst_dir[] = "0";
    struct proc_dir_entry *inst = NULL;
    struct mt_gpu_s *mt_gpu_i = NULL;

    mt_gpu_i = mt_global->gpus[gpu_id];
    inst_dir[0] = gpu_id + '0';
    proc_insts[gpu_id] = proc_mkdir(inst_dir, proc_root);
    if (!proc_insts[gpu_id])
    {
        sgpu_errorf("[%s] failed to create procfs node %s\n", __FUNCTION__,
                inst_dir);
        ret = -EFAULT;
        goto err;
    }

    inst = proc_create_data(PROCFS_MAX_INST_NODE, 0666,
                            proc_insts[gpu_id], &sgpu_max_inst_node_fops, (void *)mt_gpu_i);
    if (!inst)
    {
        sgpu_errorf("[%s] failed to create node \"%s\"\n", __FUNCTION__, PROCFS_MAX_INST_NODE);
        ret = -EFAULT;
        goto err;
    }

    inst = proc_create_data(PROCFS_POLICY_NODE, 0666,
                            proc_insts[gpu_id], &sgpu_policy_node_fops, (void *)mt_gpu_i);
    if (!inst)
    {
        sgpu_errorf("[%s] failed to create node \"%s\"\n", __FUNCTION__, PROCFS_POLICY_NODE);
        ret = -EFAULT;
        goto err;
    }

    inst = proc_create_data(PROCFS_TIME_SLICE_NODE, 0666,
                            proc_insts[gpu_id], &sgpu_time_slice_node_fops, (void *)mt_gpu_i);
    if (!inst)
    {
        sgpu_errorf("[%s] failed to create node \"%s\"\n", __FUNCTION__, PROCFS_TIME_SLICE_NODE);
        ret = -EFAULT;
        goto err;
    }

    inst = proc_create_data(PROCFS_OVERCOMMIT_RATIO, 0666,
                            proc_insts[gpu_id], &sgpu_overcommit_ratio_node_fops, (void *)mt_gpu_i);
    if (!inst)
    {
        sgpu_errorf("[%s] failed to create node \"%s\"\n", __FUNCTION__, PROCFS_OVERCOMMIT_RATIO);
        ret = -EFAULT;
        goto err;
    }

    return 0;
err:
    return ret;
}

int sgpu_km_procfs_init(void)
{
    int i = 0;
    int ret = 0;
    int gpu_id = 0;

    proc_root = proc_mkdir(PROCFS_ROOT_DIR, NULL);
    if (!proc_root)
    {
        sgpu_errorf("[%s] failed to create procfs node %s\n", __FUNCTION__,
               PROCFS_ROOT_DIR);
        return -EFAULT;
    }

    if (gpu_ids_num >= 1) {
        total_gpu_num = gpu_ids_num;
    }

    for (i = 0; i < total_gpu_num; i++)
    {
        gpu_id = i;
        if (gpu_ids_num >= 1) {
            gpu_id = gpu_ids[i];
        }

        ret = sgpu_km_procfs_gpu_init(gpu_id);
        if (ret)
        {
            goto err;
        }
    }

    node_cores = proc_create_data(PROCFS_CORES_NODE, 0666, proc_root,
                                     &sgpu_cores_node_fops, NULL);
    if (!node_cores)
    {
        sgpu_errorf("[%s] failed to create procfs node %s\n", __FUNCTION__,
               PROCFS_CORES_NODE);
        ret = -EFAULT;
        goto err;
    }

    node_inst_ctl = proc_create_data(PROCFS_INST_CTL_NODE, 0222, proc_root,
                                     &sgpu_inst_ctl_fops, NULL);
    if (!node_inst_ctl)
    {
        sgpu_errorf("[%s] failed to create procfs node %s\n", __FUNCTION__,
               PROCFS_INST_CTL_NODE);
        ret = -EFAULT;
        goto err;
    }

    node_major = proc_create_data(PROCFS_MAJOR_NODE, 0444,
                                  proc_root, &sgpu_major_node_fops, NULL);
    if (!node_major)
    {
        sgpu_errorf("[%s] failed to create procfs node %s\n", __FUNCTION__,
               PROCFS_MAJOR_NODE);
        ret = -EFAULT;
        goto err;
    }
    node_major_ctl = proc_create_data(PROCFS_MAJOR_CTL_NODE, 0444,
                                      proc_root, &sgpu_major_node_ctl_fops, NULL);
    if (!node_major_ctl)
    {
        sgpu_errorf("[%s] failed to create procfs node %s\n", __FUNCTION__,
               PROCFS_MAJOR_CTL_NODE);
        ret = -EFAULT;
        goto err;
    }

    node_version = proc_create_data("version", 0444,
                                    proc_root, &sgpu_version_node_fops, NULL);
    return 0;

err:
    proc_remove(proc_root);
    proc_root = NULL;

    return ret;
}

int sgpu_km_procfs_deinit(void)
{
    if (proc_root)
    {
        proc_remove(proc_root);
        proc_root = NULL;
    }

    return 0;
}

char *sgpu_km_get_cores(void)
{
    int i = 0, j = 0;
    int cnt = 0;
    char *cores = NULL;
    mt_gpu_t *mt_gpu_i = NULL;

    for (i = 0; i < MAX_GPU; i++)
    {
        mt_gpu_i = mt_global->gpus[i];
        if (mt_gpu_i != NULL)
        {
            cnt ++;
        }
    }

    cores = sgpu_kmalloc(cnt * 2);
    if (!cores)
    {
        sgpu_errorf("failed to allocate memory for cores, length %d\n", cnt * 2 - 1);
        return get_error(ENOMEM);
    }

    for (i = 0; i < MAX_GPU; i++)
    {
        mt_gpu_i = mt_global->gpus[i];
        if (mt_gpu_i != NULL)
        {
            if (cores != NULL && strlen(cores) != 0)
            {
                cores[j++] = ',';
            }
            cores[j++] = i + '0';
        }
    }

    if (cnt == 0) {
        return "";
    }

    cores[cnt*2-1] = '\0';
    return cores;
}

int sgpu_km_set_cores(char *cores)
{
    int i, core, ret = 0;
    int **diff_arr = NULL;
    int *diff_sizes = NULL, *add_core_arr = NULL, *del_core_arr = NULL;
    int add_core_arr_size = 0, del_core_arr_size = 0;
    int cur_core_arr[MAX_GPU] = {-1}, desired_core_arr[MAX_GPU] = {-1};
    int cur_core_arr_size = 0, desired_core_arr_size = 0;
    mt_gpu_t *mt_gpu_i = NULL;

    for (i = 0; i < strlen(cores); i++)
    {
        if ((cores[i] != ',' && cores[i] != '\n' && !(cores[i] >= '0' && cores[i] < MAX_GPU + '0'))
            || (i==strlen(cores)-1 && cores[i] == ',')
            || (i<strlen(cores)-1 && cores[i] == '\n')
            || (i<strlen(cores)-1 && cores[i] == ',' && (cores[i+1] == ',' || cores[i+1] == '\n'))
            || (i<strlen(cores)-1 && (cores[i] >= '0' && cores[i] < MAX_GPU + '0') && (cores[i+1] >= '0' && cores[i+1] < MAX_GPU + '0'))
        )
        {
            sgpu_errorf("[%s] invalid cores format: %s", __FUNCTION__, cores);
            ret = -EINVAL;
            goto exit;
        }

        if (cores[i] == ',' || cores[i] == '\n')
        {
            continue;
        }

        core = cores[i] - '0';
        desired_core_arr[desired_core_arr_size++] = core;
    }

    for (i = 0; i < MAX_GPU; i++)
    {
        mt_gpu_i = mt_global->gpus[i];
        if (mt_gpu_i != NULL)
        {
            cur_core_arr[cur_core_arr_size++] = i;
        }
    }

    find_diff(cur_core_arr, cur_core_arr_size, desired_core_arr, desired_core_arr_size, &diff_arr, &diff_sizes);
    if (diff_arr == NULL || diff_sizes == NULL || diff_sizes[0] < 0 || diff_sizes[1] < 0 || diff_arr[0] == NULL || diff_arr[1] == NULL)
    {
        sgpu_errorf("[%s] failed to find diff", __FUNCTION__);
        ret = -EINVAL;
        goto exit;
    }

    del_core_arr = diff_arr[0];
    add_core_arr = diff_arr[1];
    del_core_arr_size = diff_sizes[0];
    add_core_arr_size = diff_sizes[1];

    for (i = 0; i < del_core_arr_size; i++)
    {
        mt_gpu_i = mt_global->gpus[del_core_arr[i]];
        if (mt_gpu_i != NULL && is_gpu_opened(mt_gpu_i))
        {
            sgpu_errorf("[%s] failed to set cores, since the core %d is inuse", __FUNCTION__, del_core_arr[i]);
            ret = -EINVAL;
            goto exit;
        }
    }

    for (i = 0; i < add_core_arr_size; i++)
    {
        int j = add_core_arr[i];
        mt_gpu_i = sgpu_initialize_gpu(j);
        if (mt_gpu_i == NULL)
        {
            ret = -EFAULT;
            goto exit;
        }
    }

    for (i = 0; i < del_core_arr_size; i++)
    {
        int j = del_core_arr[i];
        proc_remove(proc_insts[j]);
        proc_insts[j] = NULL;
        sgpu_finalize_gpu(j);
        mt_global->gpus[j] = NULL;
    }

    for (i = 0; i < add_core_arr_size; i++)
    {
        int j = add_core_arr[i];
        mt_global->gpus[j] = mt_gpu_i;
        ret = sgpu_km_procfs_gpu_init(j);
        if (ret)
        {
            sgpu_errorf("[%s] failed to add core node\n", __FUNCTION__);
            goto exit;
        }
    }

    gpu_ids_num = desired_core_arr_size;
    total_gpu_num = gpu_ids_num;
    for (i = 0; i < total_gpu_num; i++)
    {
        gpu_ids[i] = desired_core_arr[i];
    }

exit:
    if (diff_arr != NULL)
    {
        sgpu_kfree(diff_arr[0]);
        sgpu_kfree(diff_arr[1]);
        sgpu_kfree(diff_arr);
    }
    if (diff_sizes != NULL)
        sgpu_kfree(diff_sizes);

    return ret;
}

int inst_get_minor(void *priv)
{
    mt_sgpu_t *mt_sgpu_i = NULL;

    if (!priv)
    {
        return -EINVAL;
    }

    mt_sgpu_i = (mt_sgpu_t *)priv;
    return mt_sgpu_i->minor;
}

uint64_t inst_get_total_mem(void *priv)
{
    mt_sgpu_t *mt_sgpu_i = NULL;

    if (priv)
    {
        mt_sgpu_i = (mt_sgpu_t *)priv;
        return mt_sgpu_i->total_memory;
    }
    return 0x60000000;
}

int inst_set_total_mem(void *priv, uint64_t total_mem)
{
    mt_sgpu_t *mt_sgpu_i = NULL;

    if (!priv)
    {
        sgpu_errorf("[%s] invalid priv\n", __FUNCTION__);
        return -EINVAL;
    }

    mt_sgpu_i = (mt_sgpu_t *)priv;
    sgpu_spin_lock(mt_sgpu_i->lock);
    if (list_empty(&mt_sgpu_i->tasks) == false)
    {
        sgpu_errorf("[%s] cannot modify memory size while running\n", __FUNCTION__);
        sgpu_spin_unlock(mt_sgpu_i->lock);
        return -EBUSY;
    }

    mt_sgpu_i->total_memory = total_mem;
    mt_sgpu_i->free_memory = total_mem;
    sgpu_spin_unlock(mt_sgpu_i->lock);
    return 0;
}

/* get proc node based on gpu_id and container_id */
void *inst_get_node(char *container_id, int gpu_id)
{
    int sgpu_id = 0;
    mt_gpu_t *mt_gpu_i = NULL;
    mt_sgpu_t *mt_sgpu_i = NULL;

    mt_gpu_i = mt_global->gpus[gpu_id];
    if (!mt_gpu_i)
    {
        sgpu_errorf("[%s] invalid gpu_id", __FUNCTION__);
        return NULL;
    }

    for (sgpu_id = 0; sgpu_id < mt_gpu_i->max_inst; sgpu_id++)
    {
        mt_sgpu_i = &(mt_gpu_i->instances[sgpu_id]);
        if (container_id == NULL)
        {
            if (strlen(mt_sgpu_i->container_id) == 0)
            {
                return mt_sgpu_i;
            }
            continue;
        }
        else if (strncmp(container_id, mt_sgpu_i->container_id, NODE_NAME_LEN) == 0)
        {
            sgpu_debugf("find in sgpu_id %d", sgpu_id);
            return mt_sgpu_i;
        }
    }

    return NULL;
}

int inst_set_weight(void *priv, int weight)
{
    mt_sgpu_t *mt_sgpu_i;
    if (!priv)
    {
        sgpu_errorf("[%s] invalid priv\n", __FUNCTION__);
        return -EINVAL;
    }
    mt_sgpu_i = (mt_sgpu_t *)priv;
    sgpu_spin_lock(mt_sgpu_i->lock);
    if (list_empty(&mt_sgpu_i->tasks) == false)
    {
        sgpu_errorf("[%s] cannot modify weight while running\n", __FUNCTION__);
        sgpu_spin_unlock(mt_sgpu_i->lock);
        return -EBUSY;
    }
    // min weight is 1
    mt_sgpu_i->weight = max(weight, 1);
    sgpu_spin_unlock(mt_sgpu_i->lock);
    return 0;
}

int inst_get_weight(void *priv)
{
    mt_sgpu_t *mt_sgpu_i;

    if (priv != NULL)
    {
        mt_sgpu_i = (mt_sgpu_t *)priv;
        return mt_sgpu_i->weight;
    }
    return 0;
}

void inst_set_name(void *priv, char *buf)
{
    mt_sgpu_t *mt_sgpu_i = NULL;

    if (!priv)
    {
        sgpu_errorf("[%s] invalid priv", __FUNCTION__);
        return;
    }

    mt_sgpu_i = (mt_sgpu_t *)priv;

    if (!buf)
    {
        mt_sgpu_i->container_id[0] = '\0';
    }
    else
    {
        strncpy(mt_sgpu_i->container_id, buf, NODE_NAME_LEN);
        mt_sgpu_i->container_id[NODE_NAME_LEN] = '\0';
    }
}

// make sure meminfo and it's entries freed
struct sgpu_meminfo *inst_get_meminfo(void *priv)
{
    mt_sgpu_t *mt_sgpu_i = NULL;
    struct sgpu_meminfo *meminfo = NULL;
    sgpu_task_t *task = NULL;
    struct list_head *curr = NULL;
    int i = 0, num = 0;

    if (!priv)
    {
        sgpu_errorf("[%s] invalid priv", __FUNCTION__);
        return NULL;
    }

    mt_sgpu_i = (mt_sgpu_t *)priv;
    meminfo = sgpu_kmalloc(sizeof(struct sgpu_meminfo));
    if (!meminfo)
    {
        sgpu_errorf("[%s] malloc error", __FUNCTION__);
        return NULL;
    }
    meminfo->free_size = mt_sgpu_i->free_memory;
    sgpu_spin_lock(mt_sgpu_i->lock);
    list_for_each(curr, &mt_sgpu_i->tasks)
        num++;
    if (num == 0)
        goto exit;
    meminfo->entries = (struct sgpu_meminfo_entry *)sgpu_kmalloc(sizeof(struct sgpu_meminfo_entry) * num);
    if (!meminfo->entries)
    {
        sgpu_errorf("[%s] malloc error", __FUNCTION__);
        num = 0;
        goto exit;
    }

    list_for_each(curr, &mt_sgpu_i->tasks)
    {
        task = (sgpu_task_t *)curr;
        meminfo->entries[i].pid = task->tgid;
        meminfo->entries[i].mem_size = task->alloc_memory;
        i++;
    }
exit:
    sgpu_spin_unlock(mt_sgpu_i->lock);
    meminfo->num = (unsigned int)num;
    return meminfo;
}

int group_get_max_inst(void *priv)
{
    mt_gpu_t *mt_gpu_i;

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        return mt_gpu_i->max_inst;
    }
    return 0;
}

int group_set_max_inst(void *priv, int max_inst)
{
    mt_gpu_t *mt_gpu_i;

    if (max_inst > 16 || max_inst < 1)
    {
        sgpu_errorf("[%s] invalid max_inst %d\n", __FUNCTION__, max_inst);
        return -EINVAL;
    }
    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        if (is_gpu_opened(mt_gpu_i)) {
            sgpu_errorf("[%s] failed to set max instance, since the gpu is inuse", __FUNCTION__);
            return -EINVAL;
        }
        mt_gpu_i->max_inst = max_inst;
    }
    return 0;
}

int group_set_policy(void *priv, int policy)
{
    mt_gpu_t *mt_gpu_i;
    if (policy != sgpu_best_effort && policy != sgpu_fair_preemption && policy != sgpu_fair_fixed)
    {
        sgpu_errorf("[%s] invalid policy %d\n", __FUNCTION__, policy);
        return -EINVAL;
    }

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        if (is_gpu_opened(mt_gpu_i)) {
            sgpu_errorf("[%s] failed to set policy, since the gpu is inuse", __FUNCTION__);
            return -EINVAL;
        }
        mt_gpu_i->policy = policy;
    }
    return 0;
}

int group_get_policy(void *priv)
{
    mt_gpu_t *mt_gpu_i;

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        return mt_gpu_i->policy;
    }
    return 0;
}

int group_set_time_slice(void *priv, int time_slice)
{
    mt_gpu_t *mt_gpu_i;
    if (time_slice < 1 || time_slice > 99)
    {
        sgpu_errorf("[%s] invalid time_slice %d\n", __FUNCTION__, time_slice);
        return -EINVAL;
    }

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        if (is_gpu_opened(mt_gpu_i)) {
            sgpu_errorf("[%s] failed to set time slice, since the gpu is inuse", __FUNCTION__);
            return -EINVAL;
        }
        mt_gpu_i->time_slice_maximum = time_slice * 1000;
        mt_gpu_i->time_slice_minimum = mt_gpu_i->time_slice_maximum - 500;
    }
    return 0;
}

int group_get_time_slice(void *priv)
{
    mt_gpu_t *mt_gpu_i;

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        return mt_gpu_i->time_slice_maximum / 1000;
    }
    return 0;
}

int group_set_overcommit_ratio(void *priv, int overcommit_ratio)
{
    mt_gpu_t *mt_gpu_i;
    if (overcommit_ratio < 100 || overcommit_ratio > 200)
    {
        sgpu_errorf("[%s] invalid overcommit_ratio %d\n", __FUNCTION__, overcommit_ratio);
        return -EINVAL;
    }

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        if (is_gpu_opened(mt_gpu_i)) {
            sgpu_errorf("[%s] failed to set overcommit ratio, since the gpu is inuse", __FUNCTION__);
            return -EINVAL;
        }
        mt_gpu_i->overcommit_ratio = overcommit_ratio;
    }
    return 0;
}

int group_get_overcommit_ratio(void *priv)
{
    mt_gpu_t *mt_gpu_i;

    if (priv != NULL)
    {
        mt_gpu_i = (mt_gpu_t *)priv;
        return mt_gpu_i->overcommit_ratio;
    }
    return 0;
}

int is_gpu_opened(mt_gpu_t * mt_gpu_i) {
    int i = 0;
    mt_sgpu_t * mt_sgpu_i = NULL;

    for (; i < MAX_SGPU; i++) {
        mt_sgpu_i = &mt_gpu_i->instances[i];
        if (is_inst_opened(mt_sgpu_i))
            return 1;
    }
    return 0;
}