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

# USB 协议栈（并入 app target，与 can/uart 同构：Kconfig 门禁 → 编译期装配；多板卡 HAL 按门禁隔离）
if(CONFIG_DUST_USB_DEVICE_STACK)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core/usb_dev_port.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core/usb_cdc_acm.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core/usb_descriptor.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core)
endif()
if(CONFIG_DUST_COM_USB)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/usb.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream)
endif()

# EtherCAT 从站（C++ 实现）
#
# core/ 按职责分四层：
#   spec/     纯数据与常量，无状态（limits / registers / wire / object / application_objects）
#   model/    对象模型（dictionary）
#   pdo/      过程数据域（process_data）
#   mailbox/  邮箱链路（mailbox / sdo / sdo_info / coe / sii_service）
#   al/       状态机与时基（state_machine / sync / sm）
#   core/     组合根（device）
if(CONFIG_DUST_COM_ETHERCAT)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ethercat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/model/ecat_dictionary.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/pdo/ecat_process_data.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/mailbox/ecat_mailbox.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/mailbox/ecat_sdo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/mailbox/ecat_sdo_info.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/mailbox/ecat_coe.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/mailbox/ecat_sii_service.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/al/ecat_state_machine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/al/ecat_sync.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/al/ecat_sm.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/ecat_device.cpp)

    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/interface)
endif()

# EtherCAT PHY 设备模块（与 gpio/pwm 同层）
#
# 三颗芯片共用 ecat_phy_mdio.cpp，所以公共部分单独一个块，
# 避免多个门禁同时打开时同一源文件被重复加入 app target。
if(CONFIG_DUST_DEV_ECAT_PHY_JL1111 OR CONFIG_DUST_DEV_ECAT_PHY_RTL8201F OR CONFIG_DUST_DEV_ECAT_PHY_TJA1101B)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy/ecat_phy_mdio.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/device/ecat_phy
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/interface)
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
