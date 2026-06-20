# Dust_Zephyr_Architecture_Drivers

底层硬件外设驱动抽象。封装 Zephyr 设备驱动 API，对外提供统一的 C++ 接口。

- **communication/** — 通讯外设类（UART、CAN、SPI、RS485、USB）
- **device/** — 设备通用抽象层（GPIO 输入输出、PWM 输出）

驱动层为基础外设操作抽象，需配合上层模块使用，不单独创建线程。

详见 [ARCHITECTURE.md](ARCHITECTURE.md)。
