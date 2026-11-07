#define _POSIX_C_SOURCE 200809L
#include "aula_battery.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void stamp(void)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[16];
    strftime(buf, sizeof buf, "%H:%M:%S", &tm);
    printf("[%s] ", buf);
}

int main(int argc, char **argv)
{
    int watch = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--watch") == 0 || strcmp(argv[i], "-w") == 0) {
            watch = 1;
        } else {
            fprintf(stderr, "用法: %s [--watch]\n", argv[0]);
            return 2;
        }
    }

    if (!watch) {
        aula_battery_t b;
        int rc = aula_battery_read(&b, 75000);   /* 设备约 60 秒推一次，留点余量 */
        if (rc < 0) {
            fprintf(stderr, "错误: %s\n", aula_battery_strerror(rc));
            return 1;
        }
        printf("%3u%%%s\n", (unsigned)b.percent, b.charging ? "  [充电中]" : "");
        return 0;
    }

    int fd = aula_battery_open();
    if (fd < 0) {
        fprintf(stderr, "错误: %s\n", aula_battery_strerror(fd));
        return 1;
    }

    int last_pct = -1;
    time_t last_print = 0;
    for (;;) {
        aula_battery_t b;
        int rc = aula_battery_get(fd, &b, -1);
        if (rc < 0) {
            fprintf(stderr, "错误: %s\n", aula_battery_strerror(rc));
            break;
        }
        time_t now = time(NULL);
        /* 数值变化，或距上次输出超过 60 秒时打印 */
        if ((int)b.percent != last_pct || now - last_print >= 60) {
            last_pct = b.percent;
            last_print = now;
            stamp();
            printf("%3u%%%s\n", (unsigned)b.percent, b.charging ? "  [充电中]" : "");
            fflush(stdout);
        }
    }
    close(fd);
    return 1;   /* 走到这里说明 get 出错，watch 模式异常退出 */
}
