/*
 * aula_battery.h — 读取 AULA SC311 无线鼠标电量（Linux hidraw）
 *
 * 依赖：Linux + hidraw，纯 POSIX C11，无第三方库。
 *
 * 协议（2.4G 接收器 a8a5:2255 的 MI_02 接口，64 字节 HID input report）：
 *   byte0   0xAA        帧头
 *   byte1   0xFA        状态帧
 *   byte2   0xAE        设备地址
 *   byte3   校验和      sum(byte4..byte63) & 0xFF
 *   byte8   子类型      0xD0 = 电量
 *   byte9   电量百分比  0..100
 *   byte10  充电状态    0 = 未充电，1 = 充电中（已实测）
 *
 * 本库只读，绝不向设备发送任何命令。
 *
 * 环境变量：
 *   AULA_HIDRAW=/dev/hidrawN   强制指定设备节点
 *   AULA_DEBUG=1               把被丢弃的报文打印到 stderr（排查用）
 */
#ifndef AULA_BATTERY_H
#define AULA_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t percent;   /* 0 .. 100 */
    bool    charging;  /* true = 正在充电 */
} aula_battery_t;

/* 找到接收器并打开，成功返回 fd，失败返回负 errno：
 *   -ENODEV  没插接收器 / 不是 a8a5:2255
 *   -EACCES  没有 /dev/hidraw* 读权限 */
int aula_battery_open(void);

/* 读一次电量：timeout_ms < 0 表示一直等；成功 0，超时 -ETIMEDOUT */
int aula_battery_get(int fd, aula_battery_t *out, int timeout_ms);

/* 打开 → 读一次 → 关闭 */
int aula_battery_read(aula_battery_t *out, int timeout_ms);

/* 错误码转中文说明 */
const char *aula_battery_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* AULA_BATTERY_H */  
