cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(zamsensdisp CXX C ASM)
pico_sdk_init()

# Waveshare ePaper Library
# add a compilation subdirectory
add_subdirectory(lib/Config)
add_subdirectory(lib/e-Paper)
add_subdirectory(lib/Fonts)
add_subdirectory(lib/GUI)
# add a header directory
include_directories(./lib/Config)
include_directories(./lib/GUI)
include_directories(./lib/e-Paper)

include_directories(./src)

# generate an executable file
add_executable(zamsensdisp 
src/main.c
src/network.c
)

# enable usb output, disable uart output
pico_enable_stdio_usb(zamsensdisp 1)
pico_enable_stdio_uart(zamsensdisp 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(zamsensdisp)

target_link_libraries(zamsensdisp ePaper GUI Fonts Config pico_stdlib hardware_spi pico_cyw43_arch_lwip_threadsafe_background hardware_adc hardware_gpio hardware_rtc)

