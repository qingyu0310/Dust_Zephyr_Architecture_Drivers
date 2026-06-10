if(CONFIG_DEV_GPIO)
	target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/gpio)
endif()

if(CONFIG_DEV_PWM)
	target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device/pwm)
endif()

if(CONFIG_COM_UART OR CONFIG_COM_UART_DMA)
	target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/uart/uart.cpp)
	target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/uart)
endif()

if(CONFIG_COM_CAN)
	target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/can/can.cpp)
	target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/can)
endif()

if(CONFIG_COM_SPI)
	target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/spi/spi.cpp)
	target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/spi)
endif()

if(CONFIG_COM_RS485)
	target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/rs485/rs485.cpp)
	target_include_directories(app PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/communication/rs485
		${CMAKE_CURRENT_SOURCE_DIR}/communication/uart
	)
endif()

if(CONFIG_COM_USB)
	target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/communication/usb/usb.cpp)
	target_include_directories(app PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/communication/usb
		${CMAKE_CURRENT_SOURCE_DIR}/communication/uart
	)
endif()

