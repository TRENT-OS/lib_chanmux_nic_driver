cmake_minimum_required(VERSION 3.13.0)

# enable new behavior introduced in CMake 3.13, where relative paths will be
# converted into absolute paths in target_sources(INTERFACE ....)
cmake_policy(SET CMP0076 NEW)

project(seos_nw_driver_tap_proxy_lib C)

add_library(${PROJECT_NAME} INTERFACE)


target_sources(${PROJECT_NAME}
    INTERFACE
        src/Seos_pico_dev_chan_mux.c
        src/SeosNwChanmuxIf.c
        src/Seos_Driver_Config.c
)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        src/include
)

target_link_libraries(${PROJECT_NAME}
    INTERFACE
        seos_core_api
        #seos_libs
)
