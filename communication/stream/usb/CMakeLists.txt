# 中间层（栈内核）
if(CONFIG_DUST_USB_DEVICE_STACK)
    zephyr_library()
    zephyr_library_sources(core/usb_dev_port.cpp core/usb_cdc_acm.cpp core/usb_descriptor.cpp)
    zephyr_library_include_directories(interface core)
    zephyr_include_directories(interface core)   # 全局暴露，供业务代码裸文件名引用
endif()
# 芯片 HAL（HPM）
if(CONFIG_DUST_USB_DEVICE_HAL_HPM)
    zephyr_library_sources(hal/hpm/usb_hal_hpm.cpp
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/components/usb/device/hpm_usb_device.c
        ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/drivers/src/hpm_usb_drv.c)
    zephyr_library_include_directories(hal/hpm)
    zephyr_include_directories(hal/hpm)
    zephyr_include_directories(${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/components/usb/device
                               ${SDK_GLUE_DIR}/../sdk_env/hpm_sdk/drivers/inc)
endif()
# Stream 适配层（应用使能 DUST_COM_USB 时编译）
if(CONFIG_DUST_COM_USB)
    zephyr_library_sources(usb.cpp)
    zephyr_library_include_directories(.)
    zephyr_include_directories(.)
    zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)   # 提供 stream.hpp
endif()
