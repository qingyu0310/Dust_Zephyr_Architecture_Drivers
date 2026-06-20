# drivers/ 架构说明

## 职责

底层硬件外设驱动抽象。封装 Zephyr 设备驱动 API，对外提供统一的 C++ 接口。

## 边界

| 管 | 不管 |
|----|------|
| 封装外设硬件操作，提供 C++ 类接口 | 不包含业务逻辑 |
| 通过 Kconfig `select` 打开 Zephyr 子系统驱动 | 不依赖 `modules/`、`algorithm/`、`topic/` |
| 初始化、收发、控制等硬件基本操作 | 不创建线程 |

## 目录结构

```
drivers/
├── communication/
├── device/
├── ARCHITECTURE.md
├── CMakeLists.txt
└── Kconfig
```

communication/
    通讯外设类。UART、CAN、SPI、RS485、USB 等。
    封装 Zephyr 通讯外设 API，提供收发和控制接口。

device/
    设备通用抽象层。GPIO 输入输出、PWM 输出等简单硬件操作。
    封装底层硬件控制，对外提供简洁的读写接口。

驱动层为基础通讯外设和简单设备操作抽象，需配合上层模块使用，不单独创建线程。

## 文件规范

每个外设一个子目录，包含 `xxx.hpp` + `xxx.cpp`。

| 文件 | 内容 |
|------|------|
| `xxx.hpp` | 类声明，公开初始化、收发、控制接口 |
| `xxx.cpp` | 基于 Zephyr API 的实现 |

规则：
- 依赖 Zephyr 设备驱动模型（`struct device`、`devicetree` API），不直接操作寄存器
- 通过 `drivers/Kconfig` 定义开关，`select` Zephyr 对应子系统
- `modules/` 和 `projects/thread/` 中的代码实例化并调用驱动类
- 不反向依赖上层

## 依赖关系

每个外设驱动通过 Kconfig `select` 开启对应的 Zephyr 子系统。依赖链在 `drivers/Kconfig` 中定义。

## 调用方

- `modules/` 中的设备管理器实例化驱动
- `projects/thread/` 中的线程代码也可直接使用驱动类
