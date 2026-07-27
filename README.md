# drivers/ — 硬件驱动层

`drivers/` 是当前工程的底层硬件访问层。

它把 Zephyr 的设备树对象和外设 API 封装成项目自己的 C++ 驱动对象，供 `modules/`、`project/thread/` 和 `cmd/` 使用。

```text
board.overlay / Kconfig
        ↓
drivers/
        ↓
设备模块 / 业务线程 / 调试工具
```

## 目录

```text
drivers/
├── communication/
│   ├── can/              CAN
│   ├── spi/              SPI
│   └── stream/
│       ├── stream.hpp    连续字节流接口
│       ├── uart/         UART DMA
│       ├── rs485/        UART + 方向 GPIO
│       └── usb/          USB CDC ACM Stream 适配
├── device/
│   ├── gpio/             GPIO 输入/输出
│   └── pwm/              PWM 输出
├── CMakeLists.txt        源文件和 include 接入
├── Kconfig               驱动能力开关
└── ARCHITECTURE.md       详细架构说明
```

## 当前驱动清单

| 驱动 | 入口类 | Kconfig | 主要特征 |
| --- | --- | --- | --- |
| GPIO 输入 | `Input` | `DEV_GPIO_INPUT` | 设备树 GPIO、连续采样稳定判定 |
| GPIO 输出 | `Output` | `DEV_GPIO_OUTPUT` | 设置电平、翻转电平 |
| PWM | `Pwm` | `DEV_PWM` | 脉宽、占空比、周期 |
| SPI | `Spi` | `COM_SPI` | 同步读、写、全双工收发 |
| CAN | `Can` | `COM_CAN` | 接收过滤器、异步回调、非等待发送 |
| UART | `UartDma` | `COM_UART_DMA` | 异步 UART、DMA 双缓冲、`Stream` |
| RS485 | `Rs485` | `COM_RS485` | UART DMA、方向 GPIO、半双工 |
| USB CDC ACM | `usb::Usb` | `COM_USB` | CDC ACM、`Stream`、USB 设备协议栈适配 |

## 两类驱动

### 简单外设封装

GPIO、PWM、SPI、CAN 主要是 Zephyr API 的 C++ 包装：

```cpp
Input input{};
Output output{};
Pwm pwm{};
Spi spi{};
Can can{};
```

这些类不解析业务协议，也不创建线程。它们通常由设备模块或项目线程持有。

### 异步通信驱动

UART、RS485、USB 需要处理接收回调、DMA 缓冲、软件队列、发送忙状态和 semaphore：

```text
硬件/Zephyr 回调
    → 驱动缓冲
    → semaphore
    → 业务线程 Read()
```

连续字节流统一参考 `drivers/communication/stream/stream.hpp`：

```cpp
virtual uint16_t Read(uint8_t* buf, uint16_t max_len) = 0;
virtual bool Send(const uint8_t* data, uint32_t len) = 0;
```

### USB 的特殊位置

USB 项目入口在：

```text
drivers/communication/stream/usb/
```

但完整 USB 设备协议栈在：

```text
D:/Zephyr/modules/user/usb
```

当前拆分为：

```text
drivers/usb wrapper
    → UsbDevPort
        → UsbCdcAcm
            → UsbHalHpm
```

drivers 侧负责 `Stream`、接收队列和项目调用接口；外部模块负责枚举、EP0、CDC 类请求和 HPM USB 控制器。

## 编译接入

驱动由 Kconfig 和 CMake 共同控制：

```text
Kconfig 打开 COM_XXX / DEV_XXX
    → select Zephyr 子系统
    → drivers/CMakeLists.txt 加入对应 .cpp
    → 具体驱动类进入固件
```

常见关系：

```text
TRD_REMOTE  → COM_UART_DMA → UartDma
TRD_PC      → COM_USB      → usb::Usb
IMU 模块    → COM_SPI      → Spi
蜂鸣器      → DEV_PWM      → Pwm
CAN 线程    → COM_CAN      → Can
```

设备的具体控制器和引脚不写死在驱动源码中，而是由 board overlay 提供：

```text
overlay
    → DEVICE_DT_GET / *_DT_SPEC_GET
        → Driver::Init(...)
```

## 调用边界

驱动层负责：

- 设备 ready 检查；
- 设备树配置应用；
- 外设基本读写；
- DMA、回调和缓冲管理；
- 错误返回和底层日志。

驱动层不负责：

- 协议解析；
- topic 发布；
- 控制算法；
- 机器人业务状态机；
- 创建项目业务线程。

例如：

```text
UartDma        只提供字节流
Remote 模块    负责遥控器协议解析

Spi            只提供 SPI 传输
IMU 设备模块   负责寄存器和采样转换

Can            只收发 CAN frame
CAN 业务线程   负责 ID 和数据字段含义
```

## 阅读入口

想了解整个驱动层，建议按这个顺序阅读：

```text
1. drivers/Kconfig
2. drivers/CMakeLists.txt
3. communication/stream/stream.hpp
4. 一个简单驱动：device/gpio 或 communication/spi
5. 一个异步驱动：communication/stream/uart
6. 一个真实调用方：project/thread/remote 或 modules/imu
7. USB：drivers/usb wrapper → D:/Zephyr/modules/user/usb
```

详细的职责、数据流、生命周期和当前代码注意事项见：

[ARCHITECTURE.md](ARCHITECTURE.md)

## 当前维护提示

- `COM_UART_DMA` 是当前 `UartDma` 源文件的实际接入开关。
- `COM_UART` 目前在 Kconfig 中存在，但没有独立的 CMake 源文件映射。
- RS485 源码仍引用旧的 `RxStream` 名称，而当前公共流接口是 `Stream`，启用前需要统一接口命名。
- 异步驱动的 `Read()` 返回的是当前可取的字节数，不保证一次对应完整协议帧。
- `Send()` 返回成功通常只代表底层传输已提交；发送完成或最终错误要看对应回调/状态。
