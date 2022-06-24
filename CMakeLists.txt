project(Ice9API)
cmake_minimum_required(VERSION 3.0)

find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES "include" "libusb" "libusb-1.0")
find_library(LIBUSB_LIBRARY usb NAMES usb usb-1.0)
find_path(FTDI_INCLUDE_DIR ftdi.h PATH_SUFFIXES "libftdi1")
find_library(FTDI_LIBRARY ftdi NAMES ftdi ftdi1)

set(LIB_SOURCES sram_flash.c mpsse.c ice9.c ftdi_stream_ice9.c)
add_library(LIB_OBJECTS OBJECT ${LIB_SOURCES})
set_target_properties(LIB_OBJECTS PROPERTIES POSITION_INDEPENDENT_CODE 1)
target_compile_options(LIB_OBJECTS PRIVATE -Wall -Werror -Wno-deprecated-declarations)
target_include_directories(LIB_OBJECTS SYSTEM BEFORE PRIVATE ${FTDI_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})

add_library(ice9 SHARED $<TARGET_OBJECTS:LIB_OBJECTS>)
set_target_properties(ice9 PROPERTIES PUBLIC_HEADER ice9.h)
target_link_libraries(ice9 ${FTDI_LIBRARY} ${LIBUSB_LIBRARY})

add_library(ice9_static STATIC  $<TARGET_OBJECTS:LIB_OBJECTS>)
set_target_properties(ice9_static PROPERTIES PUBLIC_HEADER ice9.h)
target_link_libraries(ice9_static ${FTDI_LIBRARY} ${LIBUSB_LIBRARY})

install(TARGETS ice9 DESTINATION lib)
install(TARGETS ice9_static DESTINATION lib)
install(FILES ice9.h DESTINATION include)
