#define _POSIX_C_SOURCE 200809L   /* 必须在所有 include 之前 */

#include "aula_battery.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AULA_REPORT_LEN       64
#define AULA_HDR              0xAA
#define AULA_TYPE_STATUS      0xFA
#define AULA_SUBTYPE_BATTERY  0xD0

/* sysfs uevent 里的 VID:PID 是 8 位十六进制 */
#define AULA_VIDPID_STR       "0000A8A5:00002255"
#define AULA_VENDOR_INTERFACE 2   /* MI_02 = 厂商配置接口 */

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0) return 1;
    for (const char *p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == n) return 1;
    }
    return 0;
}

static int read_uevent(const char *sysname, char *buf, size_t len)
{
    char path[512];
    snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", sysname);
    FILE *f = fopen(path, "r");
    if (!f) {
        int e = errno;
        return -(e ? e : EIO);       /* errno 为 0 时不能返回 0 */
    }
    size_t n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = '\0';
    return 0;
}

/* HID_PHYS 形如 "usb-0000:00:14.0-4/input2"，inputN 就是接口号 */
static int phys_interface_is(const char *uevent, int iface)
{
    const char *p = strstr(uevent, "/input");
    if (!p) return 0;
    p += 6;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    return end != p && v == iface;
}

static int find_device(char *path, size_t path_len)
{
    const char *override = getenv("AULA_HIDRAW");
    if (override && *override) {
        snprintf(path, path_len, "%s", override);
        return 0;
    }

    DIR *dir = opendir("/sys/class/hidraw");
    if (!dir) return -errno;

    int rc = -ENODEV;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "hidraw", 6) != 0) continue;

        char ue[1024];
        if (read_uevent(de->d_name, ue, sizeof ue) < 0) continue;
        if (!contains_ci(ue, AULA_VIDPID_STR)) continue;
        if (!phys_interface_is(ue, AULA_VENDOR_INTERFACE)) continue;

        snprintf(path, path_len, "/dev/%s", de->d_name);
        rc = 0;
        break;
    }
    closedir(dir);
    return rc;
}

int aula_battery_open(void)
{
    char path[512];
    int rc = find_device(path, sizeof path);
    if (rc < 0) return rc;

    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    return (fd < 0) ? -errno : fd;
}

static int checksum_ok(const uint8_t *pkt)
{
    unsigned sum = 0;
    for (int i = 4; i < AULA_REPORT_LEN; ++i) sum += pkt[i];
    return ((sum & 0xFFu) == pkt[3]);
}

static int debug_enabled(void)
{
    static int cached = -1;
    if (cached < 0) cached = (getenv("AULA_DEBUG") != NULL);
    return cached;
}

static void dump_dropped(const uint8_t *pkt)
{
    if (!debug_enabled()) return;
    fprintf(stderr, "[aula] drop: ");
    for (int i = 0; i < AULA_REPORT_LEN; ++i) fprintf(stderr, "%02x ", pkt[i]);
    fputc('\n', stderr);
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int aula_battery_get(int fd, aula_battery_t *out, int timeout_ms)
{
    if (fd < 0 || out == NULL) return -EINVAL;

    const int forever = (timeout_ms < 0);
    const int64_t deadline = forever ? 0 : now_ms() + timeout_ms;

    for (;;) {
        int wait = -1;
        if (!forever) {
            int64_t remain = deadline - now_ms();
            if (remain <= 0) return -ETIMEDOUT;
            wait = (int)remain;
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, wait);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (pr == 0) return -ETIMEDOUT;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -EIO;

        uint8_t pkt[AULA_REPORT_LEN];
        ssize_t n = read(fd, pkt, sizeof pkt);
        if (n == 0) return -EIO;     /* 设备已消失：不能 continue，否则空转 */
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            return -errno;
        }
        if (n != AULA_REPORT_LEN) continue;

        if (pkt[0] != AULA_HDR || pkt[1] != AULA_TYPE_STATUS ||
            pkt[8] != AULA_SUBTYPE_BATTERY || !checksum_ok(pkt)) {
            dump_dropped(pkt);
            continue;
        }

        out->percent  = (uint8_t)((pkt[9] > 100) ? 100 : pkt[9]);
        out->charging = (pkt[10] != 0);
        return 0;
    }
}

int aula_battery_read(aula_battery_t *out, int timeout_ms)
{
    int fd = aula_battery_open();
    if (fd < 0) return fd;
    int rc = aula_battery_get(fd, out, timeout_ms);
    close(fd);
    return rc;
}

const char *aula_battery_strerror(int err)
{
    switch (err) {
    case 0:          return "成功";
    case -ENODEV:    return "找不到接收器（a8a5:2255 的 MI_02 接口）";
    case -EACCES:    return "没有 /dev/hidraw* 读权限（加 udev 规则或用 sudo）";
    case -ETIMEDOUT: return "等待电量报文超时（开关一下鼠标再试）";
    case -EINVAL:    return "参数无效";
    case -EIO:       return "设备 I/O 错误（接收器可能被拔出）";
    default:         return strerror(-err);
    }
}
