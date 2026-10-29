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

# EtherCAT 从站（倍福 SSC 5.13；分层照 USB：interface / core / appl / 顶层）
# ssc/digital_io.c 不编译：它是示例 application（含 main 与 APPL_* 实现），
# 本层的 appl/ecat_appl_hooks.cpp 已接管全部 APPL_* 钩子，
# 对象字典从 digital_ioObjects.h 取（由 core/ecat_pdo.cpp 展开定义）。
# HAL 与 PHY 驱动在子树 zephyr_user/drivers/ethercat/。
if(CONFIG_DUST_COM_ETHERCAT)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ethercat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/ecat_pdo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/ecat_eeprom.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core/ecat_stack.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/appl/ecat_appl_hooks.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/ecatslv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/ecatappl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/ecatcoe.c
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/coeappl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/mailbox.c
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/objdef.c
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc/sdoserv.c)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/core
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/ethercat/ssc)
endif()
