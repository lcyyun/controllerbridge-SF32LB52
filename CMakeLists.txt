cmake_minimum_required(VERSION 3.16)

set(SF32LB52_BOARD "SF32LB52_LCD_N16R8" CACHE STRING "SiFli board target")
set(SF32LB52_PROFILE "ns2" CACHE STRING "Input/output profile")
set(SIFLI_SDK_ROOT "" CACHE PATH "Path to the SiFli SDK or BSP root")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (NOT SF32LB52_BOARD STREQUAL "SF32LB52_LCD_N16R8")
    message(FATAL_ERROR "Unsupported SF32LB52_BOARD='${SF32LB52_BOARD}'. Default and only scaffolded board is SF32LB52_LCD_N16R8.")
endif ()

if (NOT SF32LB52_PROFILE STREQUAL "ns2")
    message(FATAL_ERROR "Unsupported SF32LB52_PROFILE='${SF32LB52_PROFILE}'. Default and only scaffolded profile is ns2.")
endif ()

if (NOT SIFLI_SDK_ROOT)
    message(FATAL_ERROR
        "SIFLI_SDK_ROOT is not set. This skeleton intentionally does not build "
        "without the SiFli SDK/RT-Thread BSP and CherryUSB integration.")
endif ()

project(ns2pro_bridge_sf32lb52 C CXX)

# BLOCKER: replace this with the real SiFli SDK include/toolchain entrypoint.
# Some SiFli RT-Thread projects are SCons-first; this CMake file is currently a
# source-list and integration outline until the SDK build contract is confirmed.
list(APPEND CMAKE_MODULE_PATH "${SIFLI_SDK_ROOT}/cmake")

add_executable(ns2pro_bridge_sf32lb52
    app/main.c
    protocol/sf32lb52_ds5_output_state.c
    protocol/sf32lb52_ns2_protocol.c
    src/ble_gatt_sifli.c
    src/ns2_profile.c
    src/platform_sifli.c
    src/sf32lb52_app.c
    src/usb_hid_cherryusb.c
    usb/sf32lb52_usb_device.c
)

target_include_directories(ns2pro_bridge_sf32lb52 PRIVATE
    include
    boards/${SF32LB52_BOARD}
    protocol
    usb
)

target_compile_definitions(ns2pro_bridge_sf32lb52 PRIVATE
    SF32LB52_BOARD_${SF32LB52_BOARD}=1
    SF32LB52_PROFILE_NS2=1
)

# TODO(SiFli SDK): link RT-Thread kernel/BSP, Bluetooth controller/host, USB PHY,
# and CherryUSB device libraries here after the SDK is vendored or configured.
