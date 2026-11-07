# AULA SC311 电量读取（Linux）

> **非官方项目** — 与 AULA 无任何关联，仅用于在 Linux 下读取设备状态。
> This project is unofficial and not affiliated with AULA.

读取 AULA SC311 三模无线鼠标电量的命令行工具与 C 库。

`Linux` · `C11` · `零第三方依赖` · `只读设备`

---

## 特性

- 一条命令读电量，也可常驻监听
- **自动发现设备节点**：按 USB VID/PID + 接口号匹配，插拔后 `hidraw` 编号变化也能找到
- **只读**：不向鼠标发送任何命令，不会改配置
- 纯 C11 + POSIX，无第三方库
- 可作为静态库链接进其它程序（C / C++ 均可）

## 支持设备

| 设备 | 连接方式 | 电量 |
|---|---|---|
| AULA SC311 | 2.4G 接收器（`a8a5:2255`） | ✅ 支持 |
| AULA SC311 | 蓝牙 | ❌ 设备不上报电量（BLE 只有 OTA 服务） |
| AULA SC311 | USB 有线 | ❓ 未验证（需要数据线） |

> 其他使用 `a8a5:2255` 接收器、且状态报文格式相同的型号，理论上也能用，但未测试。

## 依赖

- Linux（使用 `/sys/class/hidraw` 与 `/dev/hidraw*`）
- C11 编译器（gcc / clang）
- make

## 编译

```bash
make        # 生成 libaula_battery.a 和 aula-battery
```

## 安装

```bash
make install PREFIX=$HOME/.local    # 装到 ~/.local/bin（免 sudo）
sudo make install                   # 装到 /usr/local/bin
```

## 用法

```bash
./aula-battery          # 读一次
./aula-battery --watch  # 常驻监听；数值变化、或距上次输出超过 60 秒时打印
```

输出示例：

```
$ ./aula-battery
 80%

$ ./aula-battery --watch
[21:30:12]  80%
[21:32:05]  79%  [充电中]
```

退出码：

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 运行时错误（找不到设备 / 没权限 / 超时） |
| 2 | 参数错误 |

## 权限配置

`/dev/hidraw*` 默认只有 root 可读。加一条 udev 规则即可免 sudo：

```bash
echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="a8a5", ATTRS{idProduct}=="2255", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/70-aula-sc311.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

> 想要更严格的权限（仅当前登录用户可读），把 `MODE="0666"` 换成 `TAG+="uaccess"`。

## 环境变量

| 变量 | 说明 |
|---|---|
| `AULA_HIDRAW=/dev/hidrawN` | 强制指定设备节点，跳过自动发现 |
| `AULA_DEBUG=1` | 把被丢弃的报文以十六进制打印到 stderr，排查协议问题时用 |

```bash
AULA_DEBUG=1 ./aula-battery
```

## 作为库使用

```c
#include "aula_battery.h"
#include <stdio.h>

int main(void)
{
    aula_battery_t b;
    int rc = aula_battery_read(&b, 30000);   /* 最多等 30 秒 */
    if (rc < 0) {
        fprintf(stderr, "%s\n", aula_battery_strerror(rc));
        return 1;
    }
    printf("%u%%%s\n", (unsigned)b.percent, b.charging ? " 充电中" : "");
    return 0;
}
```

编译：

```bash
cc -O2 -std=c11 myapp.c libaula_battery.a -o myapp
```

需要频繁读取时，打开一次、循环调用：

```c
int fd = aula_battery_open();
if (fd < 0) { /* 处理错误 */ }

for (;;) {
    aula_battery_t b;
    if (aula_battery_get(fd, &b, 5000) == 0) {
        /* 更新 UI / 打印 */
    }
}
close(fd);
```

### API

| 函数 | 说明 |
|---|---|
| `int aula_battery_open(void)` | 找到接收器并打开，返回 fd；失败返回负 errno |
| `int aula_battery_get(int fd, aula_battery_t *out, int timeout_ms)` | 读一次电量；`timeout_ms < 0` 一直等 |
| `int aula_battery_read(aula_battery_t *out, int timeout_ms)` | 打开 → 读一次 → 关闭 |
| `const char *aula_battery_strerror(int err)` | 错误码转中文说明 |

```c
typedef struct {
    uint8_t percent;   /* 0 .. 100 */
    bool    charging;  /* 是否充电中 */
} aula_battery_t;
```

头文件带 `extern "C"`，C++ 项目可直接 `#include "aula_battery.h"`。

## 工作原理

```
/sys/class/hidraw/hidraw*       遍历所有 HID 节点
        │
        ├─ 读 device/uevent
        │    HID_ID   = 0003:0000A8A5:00002255   ← 匹配 VID/PID
        │    HID_PHYS = usb-.../input2           ← 匹配接口号 2 (MI_02)
        │
        ▼
   /dev/hidrawN  ── 只读打开（O_RDONLY | O_NONBLOCK）
        │
        ▼
   poll() 等待报文 → 校验帧头/类型/校验和 → 取出 byte9 电量
```

不写死 `hidrawN` 编号，因为该编号会随插拔变化。

## 协议

接收器 `a8a5:2255` 的 **MI_02** 接口（Usage Page `0xFF01`，Usage `0x10`），
**64 字节定长 HID 报文，无 Report ID**。

本工具只解析设备主动上报的状态帧（端点 `0x83`），不向设备发送任何命令。

### 帧格式（设备 → 主机）

```
偏移   +0    +1    +2        +3       +4     +5..+7   +8 ........ +63
       AA    类型  设备地址   校验和    长度    保留      数据区
```

| 偏移 | 字段 | 说明 |
|---|---|---|
| `+0` | 帧头 | 固定 `0xAA` |
| `+1` | 类型 | `0xFA` = 状态帧；`0xE6` = 信息帧；`0xED` = 未解析 |
| `+2` | 设备地址 | `0xAE` = 鼠标；`0xA5` = 接收器；`0x00` = 接收器自身 |
| `+3` | 校验和 | `sum(byte[4..63]) & 0xFF` |
| `+4` | 数据长度 | 数据区字节数 |
| `+5..7` | 保留 | 一般为 0 |
| `+8..` | 数据区 | 长度由 `+4` 决定，不足 64 字节用 `0x00` 填充 |

### 状态帧（`0xFA`）

| 偏移 | 字段 | 取值 |
|---|---|---|
| `+8` | 子类型 | `0xD0` = 电量；`0x10` = 模式/档位 |
| `+9` | 数值 | 电量 **0..100**（超过 100 时按 100 处理） |
| `+10` | 标志 | 电量帧：`0` = 未充电，非 `0` = 充电中 |

> 识别电量帧只看 `+0 == 0xAA`、`+1 == 0xFA`、`+8 == 0xD0` 三个条件，
> 地址字节是 `0xAE` 还是 `0xA5` 都能解析。

### 校验和

```
checksum = (byte[4] + byte[5] + ... + byte[63]) & 0xFF
```

## 已知限制

- 接收器约每 **60 秒**推送一次电量；`aula-battery` 默认最多等 75 秒，`--watch` 模式每分钟更新一次。
- `byte10` 充电状态已确认：`0` = 未充电，`1` = 充电中。
- 电量由设备内部估算，开关机后短时间内可能跳变，稳定后误差约 ±1%。
- 校验和规则已在设备上报的报文上验证；如遇异常可用 `AULA_DEBUG=1` 观察被丢弃的报文。
- 仅支持 Linux（依赖 hidraw）。
- 蓝牙模式不支持（设备在该模式下不上报电量）。

## 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| `找不到接收器（a8a5:2255 的 MI_02 接口）` | 确认插的是 **2.4G 接收器**，`lsusb \| grep a8a5` 应能看到 `2255` |
| `没有 /dev/hidraw* 读权限` | 加 udev 规则（见上），或 `sudo` 运行 |
| `等待电量报文超时` | 设备约 60 秒推一次，稍等重试；仍不行就开关一次鼠标，或用 `AULA_DEBUG=1` 看被丢弃的报文 |
| 找到的是其它接收器 | 用 `AULA_HIDRAW=/dev/hidrawN` 显式指定 |
| 电量一直不变 | 鼠标可能已休眠，按一下键唤醒 |

## 目录结构

```
.
├── aula_battery.h        # 库接口
├── aula_battery.c        # 设备发现 + 报文解析
├── aula_battery_cli.c    # 命令行示例
├── Makefile
├── README.md
├── LICENSE
└── .gitignore
```

## 协议说明

协议字段按设备正常工作时的通信数据整理，并在 Linux 上用同一接收器做了交叉验证。

本项目与设备厂商无任何关联，仅用于个人在 Linux 下查看电量。

## License

[MIT](LICENSE)
