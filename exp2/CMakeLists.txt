#
#
# Tencent is pleased to support the open source community by making tRPC available.
#
# Copyright (C) 2023 THL A29 Limited, a Tencent company.
# All rights reserved.
#
# If you have downloaded a copy of the tRPC source code from Tencent,
# please note that tRPC source code is licensed under the  Apache 2.0 License,
# A copy of the Apache 2.0 License is included in this file.
#
#
cmake_minimum_required(VERSION 3.16)
project(trpc_file_transfer_experiment CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRPC_INSTALL_PATH /usr/local/trpc-cpp/trpc)
# 加载tRPC-Cpp头文件及库路径
include(${TRPC_INSTALL_PATH}/cmake/config/trpc_config.cmake)
include_directories(${INCLUDE_INSTALL_PATHS})
link_directories(${LIBRARY_INSTALL_PATHS})

# 添加trpc库及其依赖的三方
set(LIBRARY trpc ${LIBS_BASIC})

# 在您的构建目标里link trpc库

set (trpc_DIR /usr/local/trpc-cpp/trpc/cmake/config)
find_package(trpc REQUIRED)

#---------------------------------------------------------------------------------------
# Compile project
#---------------------------------------------------------------------------------------
project(http_experiment CXX)

# compile server
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/server/*.cc)
add_executable(http_upload_download_server ${SRC_FILES})
target_link_libraries(http_upload_download_server ${LIBRARY})

# compile client
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/client/download_client.cc)
add_executable(download_client ${SRC_FILES})
target_link_libraries(download_client ${LIBRARY})

# compile gateway
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/gateway/*.cc)
add_executable(http_gateway ${SRC_FILES})
target_link_libraries(http_gateway ${LIBRARY})

file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/client/upload_client.cc)
add_executable(upload_client ${SRC_FILES})
target_link_libraries(upload_client ${LIBRARY})
