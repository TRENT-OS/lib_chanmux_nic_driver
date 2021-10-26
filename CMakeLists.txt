#
# ChanMux NIC Driver Library
#
# Copyright (C) 2019-2020, HENSOLDT Cyber GmbH
#
cmake_minimum_required(VERSION 3.13.0)

# enable new behavior introduced in CMake 3.13, where relative paths will be
# converted into absolute paths in target_sources(INTERFACE ....)
cmake_policy(SET CMP0076 NEW)


project(chanmux_nic_driver C)

add_library(${PROJECT_NAME} INTERFACE)


target_sources(${PROJECT_NAME}
    INTERFACE
        src/chanmux_nic_drv_cfg.c
        src/chanmux_nic_drv.c
        src/chanmux_nic_ctrl.c
)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        include
)

target_link_libraries(${PROJECT_NAME}
    INTERFACE
        os_core_api
        lib_debug
        chanmux_client
)
