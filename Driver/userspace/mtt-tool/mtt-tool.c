/*
 * MTT S30 GPU Management Tool
 * Fedora Linux compatible
 *
 * Queries GPU status via /proc/sgpu_km/ and /dev/mtgpu.* interfaces.
 *
 * Copyright © 2024 Moore Threads Inc. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define PROC_PATH       "/proc/sgpu_km"
#define DEV_MTGPU_PFX   "/dev/mtgpu."
#define HWMON_PATH      "/sys/class/drm/"
#define MAX_LINE        4096
#define MAX_GPUS        16

/* ── Forward declarations ─────────────────────────────────────────── */
static int  cmd_info(void);
static int  cmd_list(void);
static int  cmd_status(void);
static int  cmd_monitor(int interval, int count);
static void print_usage(const char *prog);

/* ── Help ─────────────────────────────────────────────────────────── */
static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTION]...\n"
        "\n"
        "MTT S30 GPU management and monitoring tool.\n"
        "\n"
        "  info              Show summary of all detected GPUs\n"
        "  list              List all GPU device nodes\n"
        "  status            Show driver and module status\n"
        "  monitor [secs]    Monitor GPU status every N seconds (default 2)\n"
        "  -h, --help        Show this help\n"
        "  -v, --version     Show version\n"
        "\n"
        "Examples:\n"
        "  %s info\n"
        "  %s monitor 5\n",
        prog, prog, prog);
}

/* ── Read a proc file into a static buffer ────────────────────────── */
static int read_proc_file(const char *path, char *buf, size_t sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return 0;
}

/* ── cmd: list ────────────────────────────────────────────────────── */
static int cmd_list(void)
{
    DIR *dir = opendir("/dev");
    if (!dir) {
        perror("opendir /dev");
        return 1;
    }

    printf("MTT S30 device nodes:\n");
    printf("%-20s %s\n", "DEVICE", "STATUS");
    printf("%-20s %s\n", "------", "------");

    struct dirent *e;
    int found = 0;
    while ((e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, "mtgpu.", 6) == 0 ||
            strcmp(e->d_name, "sgpu-km") == 0) {
            char path[128];
            snprintf(path, sizeof(path), "/dev/%s", e->d_name);
            struct stat st;
            if (stat(path, &st) == 0)
                printf("%-20s present\n", e->d_name);
            else
                printf("%-20s missing\n", e->d_name);
            found++;
        }
    }
    closedir(dir);

    if (!found) printf("  (no MTT device nodes found)\n");

    /* Check DRM devices */
    dir = opendir("/sys/class/drm");
    if (dir) {
        while ((e = readdir(dir)) != NULL) {
            if (strstr(e->d_name, "card") && e->d_type == DT_LNK) {
                char path[256], vendor[64] = {0};
                snprintf(path, sizeof(path),
                         "/sys/class/drm/%s/device/vendor", e->d_name);
                int fd = open(path, O_RDONLY);
                if (fd >= 0) {
                    char buf[16] = {0};
                    if (read(fd, buf, sizeof(buf) - 1) > 0) {
                        buf[strcspn(buf, "\n")] = 0;
                        if (strstr(buf, "0x1ed5"))
                            printf("%-20s (DRM, MTT GPU)\n", e->d_name);
                    }
                    close(fd);
                }
            }
        }
        closedir(dir);
    }

    return 0;
}

/* ── cmd: info ────────────────────────────────────────────────────── */
static int cmd_info(void)
{
    /* Use lspci for GPU info */
    FILE *fp = popen("lspci -nn | grep -i \"1ed5:\" 2>/dev/null || echo "
                     "\"(no MTT GPU found via lspci)\"", "r");
    if (!fp) return 1;

    printf("MTT S30 GPU Information:\n");
    printf("=======================\n");
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp))
        fputs(line, stdout);
    pclose(fp);
    printf("\n");

    /* Show proc interface */
    struct stat st;
    if (stat(PROC_PATH, &st) == 0) {
        printf("Proc interface  : %s (available)\n", PROC_PATH);
    } else {
        printf("Proc interface  : %s (not available - driver not loaded)\n",
               PROC_PATH);
    }

    /* Show loaded modules */
    fp = popen("lsmod | grep -E 'sgpu|mtgpu' 2>/dev/null || "
               "echo \"(no sgpu/mtgpu modules loaded)\"", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp))
            fputs(line, stdout);
        pclose(fp);
    }

    return 0;
}

/* ── cmd: status ──────────────────────────────────────────────────── */
static int cmd_status(void)
{
    printf("MTT S30 Driver Status:\n");
    printf("======================\n\n");

    /* Module loaded? */
    FILE *fp = popen("lsmod 2>/dev/null", "r");
    int mtgpu_found = 0, sgpu_found = 0;
    if (fp) {
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "mtgpu_stub")) mtgpu_found = 1;
            if (strstr(line, "sgpu_km"))    sgpu_found = 1;
        }
        pclose(fp);
    }

    printf("  mtgpu_stub : %s\n", mtgpu_found ? "LOADED" : "not loaded");
    printf("  sgpu_km    : %s\n", sgpu_found ? "LOADED" : "not loaded");
    printf("\n");

    /* Proc entries */
    DIR *dir = opendir(PROC_PATH);
    if (dir) {
        printf("Proc entries (%s):\n", PROC_PATH);
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (e->d_name[0] == '.') continue;
            printf("  %s\n", e->d_name);
        }
        closedir(dir);
    } else {
        printf("Proc interface %s: not available\n", PROC_PATH);
    }
    printf("\n");

    /* Device nodes */
    printf("Device nodes:\n");
    const char *devs[] = {"/dev/mtgpu.0", "/dev/sgpu-km", NULL};
    for (int i = 0; devs[i]; i++) {
        struct stat st;
        printf("  %-20s %s\n", devs[i],
               stat(devs[i], &st) == 0 ? "present" : "absent");
    }

    /* Hwmon */
    printf("\nHwmon:\n");
    fp = popen("find /sys/class/hwmon -name \"name\" -exec grep -l mtgpu {} \\; "
               "2>/dev/null | head -3", "r");
    if (fp) {
        int hw = 0;
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            printf("  %s\n", line);
            hw++;
        }
        if (!hw) printf("  (none found)\n");
        pclose(fp);
    }

    return 0;
}

/* ── cmd: monitor ─────────────────────────────────────────────────── */
static int cmd_monitor(int interval, int count)
{
    printf("MTT S30 Monitor (every %ds, %s)\n",
           interval, count > 0 ? "press Ctrl+C to stop" : "single shot");
    printf("======================================\n\n");

    int iterations = (count > 0) ? count : 1;

    for (int i = 0; i < iterations; i++) {
        FILE *fp = popen(
            "echo \"=== GPU ===\" && "
            "lspci -nn | grep \"1ed5:\" 2>/dev/null && "
            "echo \"=== Modules ===\" && "
            "lsmod | grep -E 'sgpu|mtgpu' 2>/dev/null || echo \"(none)\" && "
            "echo \"=== Proc ===\" && "
            "ls /proc/sgpu_km/ 2>/dev/null || echo \"(no proc)\" && "
            "echo \"=== Devices ===\" && "
            "ls /dev/mtgpu* /dev/sgpu* 2>/dev/null || echo \"(no devices)\"",
            "r");

        if (fp) {
            char line[MAX_LINE];
            while (fgets(line, sizeof(line), fp))
                fputs(line, stdout);
            pclose(fp);
        }

        if (i + 1 < iterations) {
            printf("\n--- waiting %ds ---\n", interval);
            sleep(interval);
        }
    }

    return 0;
}

/* ── main ─────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    static const struct option opts[] = {
        {"help",    no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    /* If no arguments, show help */
    if (argc < 2) {
        cmd_status();
        return 0;
    }

    /* Parse options */
    int c;
    while ((c = getopt_long(argc, argv, "hv", opts, NULL)) != -1) {
        switch (c) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'v':
            printf("mtt-tool version 1.1.1 (MTT S30 Driver)\n");
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Sub-commands */
    if (optind < argc) {
        if (strcmp(argv[optind], "info") == 0)
            return cmd_info();
        if (strcmp(argv[optind], "list") == 0)
            return cmd_list();
        if (strcmp(argv[optind], "status") == 0)
            return cmd_status();
        if (strcmp(argv[optind], "monitor") == 0) {
            int interval = 2;
            int count = 0;
            if (optind + 1 < argc) {
                interval = atoi(argv[optind + 1]);
                if (interval < 1) interval = 2;
                count = -1; /* continuous */
                if (optind + 2 < argc) {
                    count = atoi(argv[optind + 2]);
                }
            }
            return cmd_monitor(interval, count);
        }
        fprintf(stderr, "Unknown command: %s\n", argv[optind]);
        print_usage(argv[0]);
        return 1;
    }

    cmd_status();
    return 0;
}
