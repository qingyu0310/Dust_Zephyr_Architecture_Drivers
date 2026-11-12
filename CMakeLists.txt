if(CONFIG_DUST_COM_CAN)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/can/can.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/can)
endif()

if(CONFIG_DUST_COM_RS485)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/rs485/rs485.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/rs485
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream
    )
endif()

if(CONFIG_DUST_COM_SPI)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/spi/spi.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/spi)
endif()

if(CONFIG_DUST_COM_UART_DMA)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart/uart.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream
    )
endif()

if(CONFIG_DUST_DEV_GPIO_INPUT)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio/input.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio)
endif()

if(CONFIG_DUST_DEV_GPIO_OUTPUT)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio/output.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio)
endif()

if(CONFIG_DUST_DEV_PWM)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/pwm/pwm.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/pwm)
endif()

# 编码器设备接口（C++ 薄包装，底层实现在子树 drivers/encoder/encoder.c）
if(CONFIG_DUST_DEV_ENCODER)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/encoder)
endif()

# USB 门面（顶层封装 Usb : Stream，上层业务直接用；协议栈在 midlayer/usb/core/）
if(CONFIG_DUST_COM_USB)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/usb.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream
        ${CMAKE_CURRENT_SOURCE_DIR}/../midlayer/usb/core
        ${CMAKE_CURRENT_SOURCE_DIR}/../../zephyr/contract
    )
endif()

# EtherCAT 门面（顶层模板 EthercatSlave<Rx,Tx>，上层业务直接用；协议栈在 midlayer/ethercat/core/）
if(CONFIG_DUST_COM_ETHERCAT)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat
        ${CMAKE_CURRENT_SOURCE_DIR}/../midlayer/ethercat/core
        ${CMAKE_CURRENT_SOURCE_DIR}/../../zephyr/contract
    )
endif()

# 驱动：can / spi / stream(uart, rs485, stream.hpp) / device。

# EtherCAT PHY 设备模块（与 gpio/pwm 同层）
#
# 三颗芯片共用 ecat_phy_mdio.cpp，所以公共部分单独一个块，
# 避免多个门禁同时打开时同一源文件被重复加入 app target。
# 契约头（ecat_esc.hpp / ecat_types.hpp / ecat_profile.hpp）在 zephyr/contract/。
if(CONFIG_DUST_DEV_ECAT_PHY_JL1111 OR CONFIG_DUST_DEV_ECAT_PHY_RTL8201F OR CONFIG_DUST_DEV_ECAT_PHY_TJA1101B)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy/ecat_phy_mdio.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy
        ${CMAKE_CURRENT_SOURCE_DIR}/../../zephyr/contract)
endif()

if(CONFIG_DUST_DEV_ECAT_PHY_JL1111)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy/jl1111/phy_jl1111.cpp)
endif()

if(CONFIG_DUST_DEV_ECAT_PHY_RTL8201F)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy/rtl8201f/phy_rtl8201f.cpp)
endif()

if(CONFIG_DUST_DEV_ECAT_PHY_TJA1101B)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy/tja1101b/phy_tja1101b.cpp)
endif()
