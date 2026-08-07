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
if(CONFIG_DUST_USB_DEVICE_HAL_HPM)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/hal/hpm/usb_hal_hpm.cpp
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/components/usb/device/hpm_usb_device.c
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/drivers/src/hpm_usb_drv.c)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/hal/hpm
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/interface
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/components/usb/device
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/drivers/inc)
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
