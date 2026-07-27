if(CONFIG_COM_CAN)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/can/can.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/can)
endif()

if(CONFIG_COM_RS485)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/rs485/rs485.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/rs485
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream
    )
endif()

if(CONFIG_COM_SPI)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/spi/spi.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/spi)
endif()

if(CONFIG_COM_UART_DMA)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart/uart.cpp)
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/uart
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream
    )
endif()

if(CONFIG_COM_USB)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/usb.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core/usb_descriptor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core/usb_device.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core/usb_cdc_acm.cpp
    )
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/interface
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/core
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/buffer
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream
    )
endif()

if(CONFIG_COM_USB_HAL_HPM)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/hal/usb_hal_hpm.cpp
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/components/usb/device/hpm_usb_device.c
    )
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/communication/stream/usb/hal
    )
    zephyr_include_directories(
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/components/usb/device
    )
endif()

if(CONFIG_DEV_GPIO_INPUT)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio/input.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio)
endif()

if(CONFIG_DEV_GPIO_OUTPUT)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio/output.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio)
endif()

if(CONFIG_DEV_PWM)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/pwm/pwm.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/pwm)
endif()
