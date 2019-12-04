cmake_minimum_required(VERSION 3.13.0)

# enable new behavior introduced in CMake 3.13, where relative paths will be
# converted into absolute paths in target_sources(INTERFACE ....)
cmake_policy(SET CMP0076 NEW)


project(seos_nw_driver_tap_proxy_lib C)

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
        seos_core_api
        seos_libs
)
