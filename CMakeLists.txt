cmake_minimum_required(VERSION 3.0)
set(CMAKE_WARN_DEPRECATED OFF)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/arduino-cmake/cmake/ArduinoToolchain.cmake)

project(mymetriful)
register_hardware_platform(/home/ananchev/.arduino15/packages/esp8266/hardware/esp8266/2.7.4/)

set(ARDUINO_DEFAULT_BOARD "generic") 
set(ARDUINO_DEFAULT_PORT /dev/ttyUSB1)
set(MY_METRIFUL "${CMAKE_SOURCE_DIR}")

add_subdirectory(fmt)
generate_arduino_firmware(mymetriful
    SKETCH "${MY_METRIFUL}"
    SERIAL screen @SERIAL_PORT@)
#print_board_list()