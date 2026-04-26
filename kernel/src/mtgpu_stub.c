/*
 * Moore Threads MTT S30 GPU - DRM Stub Driver
 *
 * Registers as a proper DRM driver so the MTT S30 appears in /dev/dri/
 * (card2 / renderD130), exports mtgpu_drm_driver_fops for sgpu_km,
 * and exposes hwmon + sysfs attributes so nvtop/btop can monitor it.
 *
 * Copyright © 2024 Moore Threads Compatibility Layer
 * Licensed under GPLv2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>

#define MTT_VENDOR_ID        0x1ED5
#define MTT_S30_2CORE_ID     0x0102
#define MTT_S30_4CORE_ID     0x0103
#define MTGPU_DRIVER_NAME    "mtgpu_stub"
#define MAX_GPU_DEVS         8

/* MTT S30 hardware constants */
#define MTT_S30_VRAM_TOTAL   (16ULL * 1024 * 1024 * 1024)  /* 16 GB */
#define MTT_S30_TEMP_IDLE    45000   /* millidegrees C */
#define MTT_S30_POWER_IDLE   15000   /* milliwatts */

/* ── Legacy /dev/mtgpu.N char devices (required by sgpu_km) ─────────────── */
static dev_t          mtgpu_devt;
static int            mtgpu_major;
static struct class  *mtgpu_class;
static struct cdev    mtgpu_cdev[MAX_GPU_DEVS];

static int mtgpu_stub_open(struct inode *i, struct file *f)    { return 0; }
static int mtgpu_stub_release(struct inode *i, struct file *f) { return 0; }
static ssize_t mtgpu_stub_read(struct file *f, char __user *b,
                                size_t c, loff_t *p)            { return 0; }
static ssize_t mtgpu_stub_write(struct file *f, const char __user *b,
                                 size_t c, loff_t *p)           { return c; }

/* Exported so sgpu_km can find it via kallsyms */
const struct file_operations mtgpu_drm_driver_fops = {
    .owner   = THIS_MODULE,
    .open    = mtgpu_stub_open,
    .release = mtgpu_stub_release,
    .read    = mtgpu_stub_read,
    .write   = mtgpu_stub_write,
    .llseek  = noop_llseek,
};
EXPORT_SYMBOL(mtgpu_drm_driver_fops);

/* ── Per-device state ────────────────────────────────────────────────────── */
struct mtgpu_device {
    struct drm_device  drm;
    struct pci_dev    *pdev;
    int                gpu_id;
    struct device     *hwmon_dev;
};

/* ── hwmon: temperature ──────────────────────────────────────────────────── */
static umode_t mtgpu_hwmon_is_visible(const void *data,
                                       enum hwmon_sensor_types type,
                                       u32 attr, int channel)
{
    return 0444;
}

static int mtgpu_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
                             u32 attr, int channel, long *val)
{
    switch (type) {
    case hwmon_temp:
        *val = MTT_S30_TEMP_IDLE;   /* millidegrees C */
        return 0;
    case hwmon_power:
        *val = MTT_S30_POWER_IDLE;  /* microwatts (hwmon convention) */
        return 0;
    case hwmon_fan:
        *val = 0;   /* fanless */
        return 0;
    default:
        return -EOPNOTSUPP;
    }
}

static int mtgpu_hwmon_read_string(struct device *dev,
                                    enum hwmon_sensor_types type,
                                    u32 attr, int channel,
                                    const char **str)
{
    switch (type) {
    case hwmon_temp:
        *str = (channel == 0) ? "edge" : "junction";
        return 0;
    case hwmon_power:
        *str = "GPU Power";
        return 0;
    default:
        return -EOPNOTSUPP;
    }
}

static const struct hwmon_channel_info * const mtgpu_hwmon_info[] = {
    HWMON_CHANNEL_INFO(temp,
        HWMON_T_INPUT | HWMON_T_LABEL,
        HWMON_T_INPUT | HWMON_T_LABEL),
    HWMON_CHANNEL_INFO(power,
        HWMON_P_INPUT | HWMON_P_LABEL),
    HWMON_CHANNEL_INFO(fan,
        HWMON_F_INPUT),
    NULL
};

static const struct hwmon_ops mtgpu_hwmon_ops = {
    .is_visible  = mtgpu_hwmon_is_visible,
    .read        = mtgpu_hwmon_read,
    .read_string = mtgpu_hwmon_read_string,
};

static const struct hwmon_chip_info mtgpu_hwmon_chip = {
    .ops  = &mtgpu_hwmon_ops,
    .info = mtgpu_hwmon_info,
};

/* ── sysfs: VRAM + utilisation (nvtop DRM backend) ──────────────────────── */
static ssize_t mem_info_vram_total_show(struct device *dev,
                                         struct device_attribute *attr,
                                         char *buf)
{
    return sysfs_emit(buf, "%llu\n", MTT_S30_VRAM_TOTAL);
}

static ssize_t mem_info_vram_used_show(struct device *dev,
                                        struct device_attribute *attr,
                                        char *buf)
{
    return sysfs_emit(buf, "%llu\n", 0ULL);
}

static ssize_t gpu_busy_percent_show(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
    return sysfs_emit(buf, "%u\n", 0);
}

static ssize_t mem_busy_percent_show(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
    return sysfs_emit(buf, "%u\n", 0);
}

static ssize_t unique_id_show(struct device *dev,
                               struct device_attribute *attr,
                               char *buf)
{
    return sysfs_emit(buf, "mtt-s30-0\n");
}

static DEVICE_ATTR_RO(mem_info_vram_total);
static DEVICE_ATTR_RO(mem_info_vram_used);
static DEVICE_ATTR_RO(gpu_busy_percent);
static DEVICE_ATTR_RO(mem_busy_percent);
static DEVICE_ATTR_RO(unique_id);

static struct attribute *mtgpu_sysfs_attrs[] = {
    &dev_attr_mem_info_vram_total.attr,
    &dev_attr_mem_info_vram_used.attr,
    &dev_attr_gpu_busy_percent.attr,
    &dev_attr_mem_busy_percent.attr,
    &dev_attr_unique_id.attr,
    NULL,
};

static const struct attribute_group mtgpu_sysfs_group = {
    .attrs = mtgpu_sysfs_attrs,
};

/* ── DRM callbacks ───────────────────────────────────────────────────────── */
static int mtgpu_drm_open(struct drm_device *dev, struct drm_file *file)
{
    return 0;
}

static void mtgpu_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
}

/* ── DRM driver descriptor ───────────────────────────────────────────────── */
static const struct drm_driver mtgpu_drm_driver = {
    .driver_features = DRIVER_RENDER | DRIVER_GEM,
    .open            = mtgpu_drm_open,
    .postclose       = mtgpu_drm_postclose,
    .name            = "mtgpu",
    .desc            = "Moore Threads MTT S30 GPU (stub)",
    .major           = 1,
    .minor           = 1,
    .patchlevel      = 1,
    .fops            = &mtgpu_drm_driver_fops,
};

/* ── PCI probe ───────────────────────────────────────────────────────────── */
static int mtgpu_pci_probe(struct pci_dev *pdev,
                            const struct pci_device_id *id)
{
    struct mtgpu_device *mdev;
    dev_t cdev_num;
    int gpu_id = -1, i, ret;

    for (i = 0; i < MAX_GPU_DEVS; i++) {
        if (mtgpu_cdev[i].dev == 0) { gpu_id = i; break; }
    }
    if (gpu_id < 0) {
        dev_err(&pdev->dev, "Too many GPUs\n");
        return -ENOSPC;
    }

    ret = pcim_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device: %d\n", ret);
        return ret;
    }

    mdev = devm_drm_dev_alloc(&pdev->dev, &mtgpu_drm_driver,
                               struct mtgpu_device, drm);
    if (IS_ERR(mdev))
        return PTR_ERR(mdev);

    mdev->pdev   = pdev;
    mdev->gpu_id = gpu_id;
    pci_set_drvdata(pdev, mdev);

    /* Register DRM device → /dev/dri/cardN + /dev/dri/renderDN */
    ret = drm_dev_register(&mdev->drm, 0);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register DRM device: %d\n", ret);
        return ret;
    }

    /* Register hwmon device → visible in /sys/class/hwmon/ as "mtgpu" */
    mdev->hwmon_dev = hwmon_device_register_with_info(
        &pdev->dev, "mtgpu", mdev, &mtgpu_hwmon_chip, NULL);
    if (IS_ERR(mdev->hwmon_dev)) {
        dev_warn(&pdev->dev, "hwmon registration failed: %ld\n",
                 PTR_ERR(mdev->hwmon_dev));
        mdev->hwmon_dev = NULL;
    }

    /* Add sysfs attributes on the PCI device (nvtop DRM backend reads these) */
    ret = sysfs_create_group(&pdev->dev.kobj, &mtgpu_sysfs_group);
    if (ret)
        dev_warn(&pdev->dev, "sysfs group creation failed: %d\n", ret);

    /* Legacy /dev/mtgpu.N char device */
    cdev_num = MKDEV(mtgpu_major, gpu_id);
    cdev_init(&mtgpu_cdev[gpu_id], &mtgpu_drm_driver_fops);
    mtgpu_cdev[gpu_id].owner = THIS_MODULE;
    if (cdev_add(&mtgpu_cdev[gpu_id], cdev_num, 1) == 0)
        device_create(mtgpu_class, NULL, cdev_num, NULL, "mtgpu.%d", gpu_id);

    dev_info(&pdev->dev,
             "MTT S30 GPU %d: card%d / renderD%d / hwmon registered\n",
             gpu_id,
             mdev->drm.primary ? mdev->drm.primary->index : -1,
             mdev->drm.render  ? mdev->drm.render->index  : -1);
    return 0;
}

/* ── PCI remove ──────────────────────────────────────────────────────────── */
static void mtgpu_pci_remove(struct pci_dev *pdev)
{
    struct mtgpu_device *mdev = pci_get_drvdata(pdev);

    if (!mdev)
        return;

    sysfs_remove_group(&pdev->dev.kobj, &mtgpu_sysfs_group);

    if (mdev->hwmon_dev)
        hwmon_device_unregister(mdev->hwmon_dev);

    if (mdev->gpu_id >= 0 && mdev->gpu_id < MAX_GPU_DEVS &&
        mtgpu_cdev[mdev->gpu_id].dev) {
        device_destroy(mtgpu_class, MKDEV(mtgpu_major, mdev->gpu_id));
        cdev_del(&mtgpu_cdev[mdev->gpu_id]);
        mtgpu_cdev[mdev->gpu_id].dev = 0;
    }

    drm_dev_unregister(&mdev->drm);
}

/* ── PCI device table ────────────────────────────────────────────────────── */
static const struct pci_device_id mtgpu_pci_ids[] = {
    { PCI_DEVICE(MTT_VENDOR_ID, MTT_S30_2CORE_ID) },
    { PCI_DEVICE(MTT_VENDOR_ID, MTT_S30_4CORE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, mtgpu_pci_ids);

static struct pci_driver mtgpu_pci_driver = {
    .name     = MTGPU_DRIVER_NAME,
    .id_table = mtgpu_pci_ids,
    .probe    = mtgpu_pci_probe,
    .remove   = mtgpu_pci_remove,
};

/* ── Module init / exit ──────────────────────────────────────────────────── */
static int __init mtgpu_stub_init(void)
{
    int ret, i;

    ret = alloc_chrdev_region(&mtgpu_devt, 0, MAX_GPU_DEVS, "mtgpu");
    if (ret < 0)
        return ret;
    mtgpu_major = MAJOR(mtgpu_devt);

    mtgpu_class = class_create("mtgpu");
    if (IS_ERR(mtgpu_class)) {
        ret = PTR_ERR(mtgpu_class);
        unregister_chrdev_region(mtgpu_devt, MAX_GPU_DEVS);
        return ret;
    }

    for (i = 0; i < MAX_GPU_DEVS; i++)
        mtgpu_cdev[i].dev = 0;

    ret = pci_register_driver(&mtgpu_pci_driver);
    if (ret) {
        class_destroy(mtgpu_class);
        unregister_chrdev_region(mtgpu_devt, MAX_GPU_DEVS);
        return ret;
    }

    pr_info("mtgpu_stub: MTT S30 loaded — DRI + hwmon + sysfs active\n");
    return 0;
}

static void __exit mtgpu_stub_exit(void)
{
    int i;

    pci_unregister_driver(&mtgpu_pci_driver);

    for (i = 0; i < MAX_GPU_DEVS; i++)
        if (mtgpu_cdev[i].dev)
            cdev_del(&mtgpu_cdev[i]);

    class_destroy(mtgpu_class);
    unregister_chrdev_region(mtgpu_devt, MAX_GPU_DEVS);
    pr_info("mtgpu_stub: unloaded\n");
}

module_init(mtgpu_stub_init);
module_exit(mtgpu_stub_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Moore Threads Compatibility Layer");
MODULE_DESCRIPTION("MTT S30 DRM stub — DRI + hwmon + nvtop/btop visibility");
MODULE_VERSION("1.1.1");
MODULE_DEVICE_TABLE(pci, mtgpu_pci_ids);
