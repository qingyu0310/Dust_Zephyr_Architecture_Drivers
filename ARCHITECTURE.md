# drivers/ 架构说明

`drivers/` 是工程的硬件访问层。

它把板级设备树、Zephyr 外设 API、异步中断/DMA 细节和少量芯片相关配置，整理成上层模块可以直接持有和调用的 C++ 对象。

当前驱动层的核心目标不是建立一个覆盖所有 MCU 的“大一统驱动框架”，而是把重复出现的硬件访问方式固定下来：

```text
设备树 / Zephyr device
        ↓
drivers/ 的 C++ 外设对象
        ↓
modules/ 或 project/thread/ 的设备管理与业务线程
        ↓
算法、协议和机器人业务
```

当前实现中有两种明显不同的驱动形态：

```text
GPIO / PWM / SPI / CAN
    → Zephyr API 的轻量 C++ 封装

UART / RS485 / USB
    → 带 DMA、回调、缓冲和状态管理的异步通信驱动
```

其中 USB 还有一个特殊边界：`drivers/communication/stream/usb/` 只保留项目侧 `Stream` 适配器，USB 设备协议栈和 HPM 硬件端口位于外部用户模块 `D:/Zephyr/modules/user/usb`。

---

## 目录结构

```text
drivers/
├── ARCHITECTURE.md
├── README.md
├── CMakeLists.txt
├── Kconfig
├── communication/
│   ├── can/
│   │   ├── can.hpp
│   │   └── can.cpp
│   ├── spi/
│   │   ├── spi.hpp
│   │   └── spi.cpp
│   └── stream/
│       ├── stream.hpp
│       ├── uart/
│       │   ├── uart.hpp
│       │   └── uart.cpp
│       ├── rs485/
│       │   ├── rs485.hpp
│       │   └── rs485.cpp
│       └── usb/
│           ├── usb.hpp
│           ├── usb.cpp
│           └── usb_rx_queue.hpp
└── device/
    ├── gpio/
    │   ├── input.hpp
    │   ├── input.cpp
    │   ├── output.hpp
    │   └── output.cpp
    └── pwm/
        ├── pwm.hpp
        └── pwm.cpp
```

目录职责如下：

| 目录 | 当前职责 | 典型调用方 |
| --- | --- | --- |
| `device/gpio/` | GPIO 输入、输出和简单状态处理 | `project/thread/gpio/`、设备模块 |
| `device/pwm/` | PWM 初始化、占空比和脉宽控制 | 蜂鸣器、加热器等设备模块 |
| `communication/can/` | CAN 过滤器、收发和回调转发 | `project/thread/can/` |
| `communication/spi/` | SPI 同步读、写和全双工收发 | IMU 设备模块、Flash 等 |
| `communication/stream/` | 面向连续字节流的公共接口和异步实现 | Remote、RS485、USB、调试通信 |
| `communication/stream/uart/` | UART 异步 DMA 双缓冲 | Remote、IMU 加热器、shell |
| `communication/stream/rs485/` | UART + 方向 GPIO 的半双工通信 | RS485 设备模块 |
| `communication/stream/usb/` | USB CDC ACM 到 `Stream` 的项目适配 | PC 通信线程、测试线程 |

---

## 驱动层负责什么

### 负责硬件访问的稳定封装

驱动对象负责：

- 保存 Zephyr `struct device`、`gpio_dt_spec`、`spi_dt_spec` 或 `pwm_dt_spec`；
- 检查底层设备是否 ready；
- 应用设备树提供的硬件配置；
- 调用 Zephyr 外设 API；
- 处理 UART/CAN/USB 的中断或异步回调；
- 管理 DMA 缓冲、软件接收缓冲、发送忙状态和完成通知；
- 把底层错误转换为 `bool`、回调或日志；
- 向上层暴露稳定、低细节的 C++ 方法。

### 不负责业务含义

驱动层不应该知道：

- 遥控器协议如何解码；
- IMU 数据如何融合；
- 底盘如何闭环；
- 云台如何控制；
- 某个 CAN ID 对应哪一个业务对象；
- 某一帧 USB 数据是不是命令、日志还是测试数据。

例如 `UartDma` 只负责把字节收进缓冲区，`modules/remotes/` 才负责把这些字节解释成遥控器协议。

### 不负责项目线程

当前驱动类本身不创建业务线程。它们通过以下方式与线程协作：

```text
同步 API
    → 调用者直接等待函数返回

异步回调
    → ISR/Zephyr 驱动回调通知上层

Stream + semaphore
    → 驱动写入缓冲区
    → sem_ 唤醒调用线程
```

`cmd/shell` 虽然拥有自己的线程，但那是调试控制面组件的特例，不属于 `drivers/` 的职责。

---

## 驱动层不应该负责什么

| 不应放入 drivers | 原因 |
| --- | --- |
| 协议解析 | 协议有自己的状态机和数据格式 |
| topic 发布 | 消息总线属于模块/应用层 |
| 业务线程 | 线程周期和优先级由项目运行时决定 |
| 控制算法 | 算法不应被某个硬件设备绑死 |
| 设备组合逻辑 | 例如“IMU + 加热器 + 校准”属于设备模块 |
| 项目配置决策 | `drivers` 只声明底层能力，项目 Kconfig 决定是否使用 |

驱动可以提供“传输完成回调”或“数据到达回调”，但不应该在回调里直接执行机器人业务。

---

## Kconfig → CMake → 源文件

驱动是否进入固件，由两层共同决定：

```text
drivers/Kconfig
    → 声明驱动能力和 Zephyr 子系统依赖

drivers/CMakeLists.txt
    → 根据 CONFIG_XXX 加入对应 .cpp 和 include 目录

源文件
    → 编译成当前固件真正使用的驱动对象
```

### 当前 Kconfig 能力

| Kconfig | 当前含义 | Zephyr 依赖 |
| --- | --- | --- |
| `DEV_GPIO_OUTPUT` | GPIO 输出封装 | `GPIO` |
| `DEV_GPIO_INPUT` | GPIO 输入封装 | `GPIO` |
| `DEV_PWM` | PWM 封装 | `PWM` |
| `COM_UART` | UART 中断/异步能力声明 | `SERIAL`、`UART_INTERRUPT_DRIVEN`、`UART_ASYNC_API`、`BUF_BIPBUF` |
| `COM_UART_DMA` | 当前 `UartDma` 实现 | `SERIAL`、`UART_ASYNC_API`、`BUF_BIPBUF` |
| `COM_SPI` | SPI 同步封装 | `SPI`、`GPIO` |
| `COM_CAN` | CAN 封装 | `CAN` |
| `COM_RS485` | UART 异步 + 方向 GPIO | `SERIAL`、`UART_ASYNC_API`、`GPIO` |
| `COM_USB` | USB CDC ACM `Stream` 封装 | `BUF_BIPBUF`、`USB` |

### 当前 CMake 映射

```cmake
CONFIG_COM_CAN       → communication/can/can.cpp
CONFIG_COM_RS485     → communication/stream/rs485/rs485.cpp
CONFIG_COM_SPI       → communication/spi/spi.cpp
CONFIG_COM_UART_DMA  → communication/stream/uart/uart.cpp
CONFIG_COM_USB       → communication/stream/usb/usb.cpp
CONFIG_DEV_GPIO_*    → device/gpio/*.cpp
CONFIG_DEV_PWM       → device/pwm/pwm.cpp
```

项目层通常通过 `select` 打开驱动。例如：

```text
TRD_PC
    → select COM_USB

TRD_REMOTE
    → select COM_UART_DMA

IMU 设备模块
    → select COM_SPI

蜂鸣器/加热器模块
    → select DEV_PWM
```

调用方不应该自己修改 `drivers/CMakeLists.txt` 来添加某个外设实例。实例的设备节点和参数放在 board overlay，驱动能力开关放在 Kconfig，调用代码只负责构造对象并初始化。

---

## 统一数据流接口：Stream

`drivers/communication/stream/stream.hpp` 定义了连续字节流的公共形态：

```cpp
class Stream
{
public:
    virtual ~Stream() = default;
    virtual uint16_t Read(uint8_t* buf, uint16_t max_len) = 0;
    virtual bool Send(const uint8_t* data, uint32_t len) = 0;

    k_sem sem_{};
};
```

当前明确沿用这一接口的实现包括：

```text
UartDma
Usb::Usb
```

RS485 当前源代码仍保留旧的 `RxStream` 命名，和现有 `stream.hpp` 的 `Stream` 不一致，见文末“当前代码注意事项”。

### Stream 的职责边界

`Stream` 只统一两件事：

```text
Read() → 从驱动维护的软件接收缓冲取字节
Send() → 向硬件提交一段连续字节
```

它不统一：

- 帧边界；
- 协议格式；
- 发送队列；
- 重传；
- 校验；
- 业务线程周期。

因此 Remote、USB 回环、shell 或其他上层代码，都需要自己决定“读到的字节如何解释”。

### semaphore 的使用约定

异步流驱动在成功写入软件接收缓冲后唤醒 `sem_`：

```text
UART DMA 回调 / USB 数据回调
    → 写入 BipBuffer 或 UsbRxQueue
    → k_sem_give(&sem_)
    → 线程 Read()
```

`sem_` 表示“可能有数据”，不是“恰好有一帧数据”。调用者仍然必须检查 `Read()` 的返回长度。

---

## 各驱动实现

## 1. GPIO

### `Input`

文件：

```text
drivers/device/gpio/input.hpp
drivers/device/gpio/input.cpp
```

初始化：

```cpp
Input input{};
input.init(GPIO_DT_SPEC_GET(DT_ALIAS(user_input), gpios));
```

`IsActivated()` 不只是读取一次 GPIO。它通过 `input_bit_` 连续记录最近的采样结果，只有低 16 位全部为 1 才返回激活：

```text
采样 1 → 移位累积
采样 2 → 移位累积
...
连续 16 次有效 → true
```

这是一种非常轻量的去抖/稳定判定。它不是 GPIO 中断驱动，也不负责自动周期采样，调用方需要按自己的周期反复调用 `IsActivated()`。

### `Output`

文件：

```text
drivers/device/gpio/output.hpp
drivers/device/gpio/output.cpp
```

公开操作：

```cpp
output.init(spec);
output.Set(true);
output.Set(false);
output.Toggle();
```

`init()` 成功后会把输出先置为低电平。输出对象不创建线程，不保存额外业务状态。

---

## 2. PWM

文件：

```text
drivers/device/pwm/pwm.hpp
drivers/device/pwm/pwm.cpp
```

`Pwm` 保存一个 `pwm_dt_spec`，初始化时先确认设备 ready，再把输出脉宽设为 0：

```text
Pwm::init()
    → device_is_ready()
    → SetPulse(0)
```

公开操作：

| API | 含义 |
| --- | --- |
| `SetPulse(pulse)` | 按当前周期设置脉宽 |
| `SetDuty(duty)` | 按 `0.0 ~ 1.0` 设置占空比 |
| `SetPeriodAndPulse(period, pulse)` | 同时修改周期和脉宽 |
| `Stop()` | 把脉宽置 0 |

`SetDuty()` 会把输入限制在 `[0, 1]`，再根据 `spec_.period` 计算脉宽。

PWM 驱动只提供信号输出，不决定蜂鸣器节奏、加热策略或电机控制策略。节奏和控制逻辑应放在 `cmd/`、`modules/` 或 `project/thread/`。

---

## 3. SPI

文件：

```text
drivers/communication/spi/spi.hpp
drivers/communication/spi/spi.cpp
```

`Spi` 是同步阻塞封装，不自己创建 DMA 或线程：

```text
Init(spi_dt_spec)
    → spi_is_ready_dt()
    → 保存设备树配置

Transceive()
    → spi_transceive_dt()

Send()
    → spi_write_dt()

Read()
    → spi_read_dt()
```

设备模块通常持有一个或多个 `Spi` 对象：

```text
Icm42688p
    → Spi spi_

Bmi088
    → Spi accel_
    → Spi gyro_
```

这种设计的意义是让 IMU 设备模块拥有自己的片选和总线配置，同时不把传感器寄存器协议放进 drivers。

SPI 层不负责：

- 寄存器地址；
- 读写命令拼接；
- 传感器初始化序列；
- 校准；
- 数据换算。

这些属于具体设备模块。

---

## 4. CAN

文件：

```text
drivers/communication/can/can.hpp
drivers/communication/can/can.cpp
```

`Can` 的主要职责：

```text
设备 ready 检查
    → 添加一个 Zephyr CAN 接收过滤器
    → 可选设置 controller mode
    → 启动 CAN
    → 把 Zephyr 回调转给 C++ 回调
```

公开接口：

```cpp
Can can{};
can.Init(dev, filter);
can.SetRxCallback(rx_cb, user_data);
can.SetTxCallback(tx_cb, user_data);
can.Send(&frame);
```

接收路径：

```text
CAN ISR/Zephyr driver
    → Can::rx_callback()
    → 保存的 RxCallback
    → 上层处理 struct can_frame
```

发送路径当前使用 `K_NO_WAIT`：

```text
Can::Send()
    → can_send(..., K_NO_WAIT, tx_callback, this)
```

因此 `Send()` 只表示“发送请求是否提交成功”，不表示总线仲裁已经完成。最终结果通过 TX callback 提供。

CAN 驱动不解释 CAN ID。`filter` 由调用方配置，收到的帧由上层协议模块解释。

---

## 5. UART DMA

文件：

```text
drivers/communication/stream/uart/uart.hpp
drivers/communication/stream/uart/uart.cpp
```

当前实际使用的 UART 实现是 `UartDma`。

### 初始化链路

```text
UartDma::Init()
    → 保存 struct device 和 Config
    → device_is_ready()
    → uart_callback_set()
    → uart_configure()
    → uart_rx_enable(dma_buf_[0])
    → ready_ = 1
```

### 接收链路

UART 使用 Zephyr 异步 UART API 和两个 DMA 缓冲：

```text
dma_buf_[0] / dma_buf_[1]
        ↓
UART_RX_RDY
        ↓
回调把数据写入 rx_bip_
        ↓
k_sem_give(&sem_)
        ↓
上层线程调用 Read()
```

`UART_RX_BUF_REQUEST` 时，驱动尝试提供另一块 DMA 缓冲；`UART_RX_BUF_RELEASED` 时更新对应缓冲的空闲状态；`UART_RX_DISABLED` 时重新启用首块缓冲。

### 发送链路

`Send()` 当前是单发送缓冲、单帧在途：

```text
检查 ready_
    → 检查 tx_busy_
        → 拷贝到 tx_buf_
            → uart_tx()
                → UART_TX_DONE
                    → 清 tx_busy_
                    → 调用 TxCallback
```

它不是发送队列。上一次发送未完成时，新的 `Send()` 会返回 `false`。

### 使用边界

`UartDma` 适合：

- 遥控器连续字节流；
- PC 调试数据；
- 需要异步接收和线程唤醒的协议；
- DMA 接收比轮询更重要的场景。

它不负责：

- 帧头/帧尾解析；
- CRC；
- 协议超时状态机；
- 收到一帧后发布 topic。

这些由 `modules/remotes/` 或具体业务线程负责。

---

## 6. RS485

文件：

```text
drivers/communication/stream/rs485/rs485.hpp
drivers/communication/stream/rs485/rs485.cpp
```

设计意图是：

```text
UART DMA
    + 方向控制 GPIO
        → 发送前切到 TX
        → UART_TX_DONE / UART_TX_ABORTED
        → 自动切回 RX
```

接收使用 DMA 双缓冲和内部环形数组，发送使用独立发送缓冲。`Send()` 在一帧发送完成前拒绝下一帧，避免方向脚在两次发送之间产生竞态。

当前 RS485 代码仍有一个需要整理的接口遗留：

```text
rs485.hpp / rs485.cpp / modules/imu/drivers/heater.cpp
    → 仍使用 RxStream

drivers/communication/stream/stream.hpp
    → 当前定义的是 Stream
```

因此 RS485 当前应视为“设计已存在、接口命名尚未和现行 Stream 统一”的代码路径。启用 `COM_RS485` 前，应先统一基类、配置类型和调用方命名。

---

## 7. USB CDC ACM

USB 是当前 drivers 层最特殊的通信驱动。

### 项目侧入口

```text
drivers/communication/stream/usb/
    ├── usb.hpp
    ├── usb.cpp
    └── usb_rx_queue.hpp
```

`usb::Usb` 继承 `Stream`，对上层只暴露：

```cpp
usb_.Init(hal_cfg, cdc_cfg);
usb_.Read(buf, len);
usb_.Send(data, len);
usb_.IsReady();
usb_.IsTxBusy();
```

### 外部协议栈

项目根 CMake 把以下目录作为外部模块和 include 路径接入：

```text
D:/Zephyr/modules/user/usb
```

外部模块负责：

```text
UsbDevPort
    → USB 设备状态机
    → EP0 标准请求
    → CDC ACM 类请求
    → CDC 描述符
    → 端点生命周期
    → UsbHal
        → HPM USB 控制器、PHY、IRQ、DMA、QHD/qTD
```

drivers 侧不重复实现 USB 协议，而是负责把 CDC 数据端点接到项目的 `Stream` 抽象。

### USB 数据接收链路

```text
HPM USB IRQ
    → UsbHalHpm
    → UsbDevPort / UsbCdcAcm
    → Usb::OnDataEvent()
    → UsbRxQueue::Push()
    → Stream::sem_
    → 上层调用 Usb::Read()
```

`UsbRxQueue` 内部使用 `BipBuffer<1024>` 和 spinlock，负责保护 ISR/线程之间的并发访问，并记录 overflow。

### USB 数据发送链路

```text
上层 Usb::Send()
    → 检查 configured_
    → 检查 CDC 是否允许发送
    → 抢占 tx_busy_
    → UsbHal::EpStartTx()
    → IN completion
    → 完整 MPS 时发送 ZLP
    → 最终清 tx_busy_
```

`Send()` 只允许一笔发送在途，不是多帧发送队列。USB reset、取消配置和端点关闭时由配置回调清理发送状态和接收队列。

### USB 为什么不直接放进普通 drivers 文件

普通 GPIO/SPI/CAN 驱动可以直接依赖 Zephyr 的 `struct device` 和标准外设 API。

当前 USB 需要处理：

- USB 枚举；
- EP0 控制传输；
- CDC ACM 类请求；
- FS/HS bulk MPS；
- DMA cache；
- EHCI QHD/qTD；
- PHY 初始化；
- IRQ 生命周期。

这些内容已经形成独立的 USB 模块。把它们全部塞回 `drivers/communication/stream/usb/` 会让项目驱动目录同时承担协议栈、芯片 HAL 和 Stream 适配三种职责。

当前拆分是：

```text
drivers/
    → 项目通信接口和 Stream 适配

modules/user/usb/
    → 可独立阅读、替换和移植的 USB 设备栈
```

---

## 设备树、Kconfig 和 C++ 初始化的分工

三者职责不同：

| 机制 | 负责内容 |
| --- | --- |
| DeviceTree / overlay | 具体控制器、GPIO、PWM、SPI 设备和硬件连接 |
| Kconfig | 是否编译某类驱动，以及需要哪些 Zephyr 子系统 |
| C++ `Init()` | 运行时保存设备规格、检查 ready、启动 DMA/控制器 |

以 UART 为例：

```text
board.overlay
    → aliases { remote-uart = &uart4; }

project/thread/Kconfig
    → select COM_UART_DMA

drivers/CMakeLists.txt
    → 编译 uart.cpp

trd_remote.cpp
    → DEVICE_DT_GET(DT_ALIAS(remote_uart))
    → UartDma::Init()
```

以 SPI IMU 为例：

```text
board.overlay
    → imu-spi alias

模块 Kconfig
    → select COM_SPI

设备模块
    → SPI_DT_SPEC_GET(...)
    → Spi::Init()
    → 设备模块自己执行寄存器读写
```

驱动不应该把设备树节点写死在实现里。驱动接收 `device` 或 `*_dt_spec`，这样同一个类可以服务不同板卡的不同节点。

---

## 初始化、回调和上下文边界

### 同步驱动

GPIO、PWM、SPI 的调用通常发生在普通线程上下文：

```text
业务线程
    → 驱动方法
        → Zephyr API
            → 返回成功/失败
```

### 异步驱动

UART、CAN、USB 的回调可能运行在 ISR 或底层驱动回调上下文。回调里应当只做必要工作：

```text
复制数据
更新状态
唤醒线程
调用轻量回调
```

不建议在底层回调里执行：

- 长时间阻塞；
- 大量日志输出；
- 复杂协议解析；
- 访问可能睡眠的 API；
- 直接修改多个业务模块状态。

### 生命周期

驱动对象通常由调用方静态持有：

```cpp
static UartDma uart{};
static Spi spi{};
static usb::Usb usb{};
```

初始化之后，调用方负责保证：

- `Init()` 的 `device` 或 `*_dt_spec` 生命周期有效；
- 回调注册对象在设备停止前仍然存在；
- DMA 正在使用的缓冲区不会被提前释放；
- 异步驱动停止后才修改线参数或重新初始化。

---

## 调用关系

当前 drivers 的典型依赖方向是：

```text
project/thread/
    ├── trd_remote.cpp
    │   └── UartDma
    ├── trd_pc.cpp
    │   └── usb::Usb
    ├── trd_can_tx.cpp
    │   └── Can
    └── trd_gpio.cpp
        └── Input / Output

modules/
    ├── remotes/
    │   └── UartDma
    ├── imu/devices/
    │   └── Spi
    └── imu/drivers/heater.cpp
        └── UartDma / Pwm
```

应保持：

```text
drivers → Zephyr API、设备树类型、必要的底层模块
modules/project → drivers
drivers -X-> modules/project 的业务类
```

USB 是明确的例外接入：

```text
drivers/usb wrapper
    → external modules/user/usb
```

这不是 drivers 反向依赖业务，而是项目通过根 CMake 装配一个独立的基础设施模块。

---

## 新增驱动的建议流程

### 1. 先确定驱动属于哪一类

```text
单次硬件操作
    → device/ 或 communication/spi、communication/can

连续字节流
    → communication/stream

协议栈或复杂控制器
    → 优先独立模块，再由 drivers 提供项目适配
```

### 2. 先设计调用方看到的接口

只暴露调用方真正需要的动作：

```cpp
Init(...)
Read(...)
Send(...)
Set(...)
Stop(...)
```

不要把寄存器、DMA 描述符或设备树宏泄漏到所有业务类。

### 3. 添加 Kconfig 和 CMake 映射

```kconfig
config COM_FOO
    bool "Foo driver"
    default n
    select FOO_SUBSYSTEM
```

```cmake
if(CONFIG_COM_FOO)
    target_sources(app PRIVATE communication/foo/foo.cpp)
    target_include_directories(app PRIVATE communication/foo)
endif()
```

### 4. 在 board overlay 提供硬件实例

驱动类接收调用方传入的设备规格，不在 `.cpp` 中固定某一个板子的节点。

### 5. 处理失败路径

至少检查：

- `device_is_ready()` 或对应 `*_is_ready_dt()`；
- 回调注册；
- RX/DMA 启动；
- TX 提交；
- stop/restart；
- 异步事件中的 buffer 和状态。

### 6. 写一个最小调用方

验证顺序应当是：

```text
设备 ready
    → Init 成功
        → 单次硬件操作成功
            → 异步接收/发送成功
                → 错误和停止路径可返回
```

不要先把业务协议、topic 和复杂线程全部接进来。

---

## 当前代码注意事项

以下不是抽象层设计建议，而是当前源码中需要维护者知道的真实状态。

### 1. `COM_UART` 暂无对应独立源文件映射

`drivers/Kconfig` 声明了 `COM_UART`，但当前 `drivers/CMakeLists.txt` 只在 `CONFIG_COM_UART_DMA` 下编译 `communication/stream/uart/uart.cpp`。

因此当前真正可直接使用 `UartDma` 的开关是：

```text
COM_UART_DMA
```

如果要保留 `COM_UART`，需要明确它是旧接口兼容项，还是补充对应实现和 CMake 映射。不要仅凭 Kconfig 名称认为该能力已经独立存在。

### 2. RS485 仍引用旧的 `RxStream`

当前公共流接口是 `Stream`，但 RS485 和加热器代码仍出现 `RxStream::Config`、`RxStream` 基类名。

这说明 RS485 路径还没有完成接口迁移。新增 RS485 功能前，应先统一：

```text
基类名称
配置结构名称
Init() 签名
调用方 include 和 Kconfig
```

### 3. 驱动对象不等于线程

`UartDma`、`Spi`、`Can`、`Usb` 都是资源和事件处理对象。它们不会替代：

- Remote 线程；
- PC 通信线程；
- CAN 发送线程；
- IMU 采样线程。

线程仍然由 `project/thread/` 或具体模块创建和调度。

### 4. 失败返回必须由调用方处理

驱动普遍采用 `bool` 返回值。`false` 可能代表：

```text
设备未 ready
参数无效
底层 API 拒绝
DMA 忙
端点未配置
发送资源不足
```

调用方不能把 `Init()` 或 `Send()` 的返回值当作“以后永远成功”的保证。

### 5. 当前多数实现是单对象、单通道语义

驱动类可以被不同模块持有，但每个对象自身代表一条具体设备通道。不要把一个 `UartDma` 对象同时交给多个不相关模块竞争使用，除非调用方额外建立访问协议。

---

## 最终判断

当前 `drivers/` 可以概括为：

```text
面向 Zephyr 设备树的 C++ 硬件访问层，
其中 Stream 统一连续字节流，
异步驱动负责缓冲和事件，
复杂 USB 协议栈独立放到外部模块，
上层模块和线程负责协议、设备语义和业务。
```

它的阅读顺序建议是：

```text
drivers/Kconfig
    → drivers/CMakeLists.txt
        → stream.hpp
            → 具体驱动头文件
                → 具体驱动 cpp
                    → project/thread 或 modules 中的调用方
```

沿着这个顺序看，能先知道“是否编译”，再知道“对象提供什么”，最后知道“真实硬件事件如何流动”。
