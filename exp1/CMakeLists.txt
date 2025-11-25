cmake_minimum_required(VERSION 3.16)
project(trpc_file_transfer_experiment CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRPC_INSTALL_PATH /usr/local/trpc-cpp/trpc)

# 加载tRPC-Cpp头文件及库路径
include(${TRPC_INSTALL_PATH}/cmake/config/trpc_config.cmake)
include_directories(${INCLUDE_INSTALL_PATHS})
link_directories(${LIBRARY_INSTALL_PATHS})

# 设置proto文件桩代码生成工具的路径 - 具体使用方式见相关章节
include(${TRPC_INSTALL_PATH}/cmake/tools/trpc_utils.cmake)
set(PB_PROTOC ${TRPC_INSTALL_PATH}/bin/protoc)
set(TRPC_CPP_PLUGIN ${TRPC_INSTALL_PATH}/bin/trpc_cpp_plugin)

# 添加trpc库及其依赖的三方
set(LIBRARY trpc ${LIBS_BASIC})

# 在您的构建目标里link trpc库

set (trpc_DIR /usr/local/trpc-cpp/trpc/cmake/config)
find_package(trpc REQUIRED)

set (Protobuf_INCLUDE_DIR "/usr/local/trpc-cpp/trpc/include")
set (Protobuf_LIBRARIES "/usr/local/trpc-cpp/trpc/lib")
find_package(Protobuf REQUIRED)

# ----------------- Protobuf 代码生成 -----------------
# 获取 .proto 文件
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto")

# 设置生成代码的输出目录
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen_proto")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# 添加一个自定义构建目标，用于从 .proto 文件生成 C++ 代码
add_custom_command(
  OUTPUT "${PROTO_GEN_DIR}/proto/file_transfer.pb.cc"
         "${PROTO_GEN_DIR}/proto/file_transfer.pb.h"
         "${PROTO_GEN_DIR}/proto/file_transfer.trpc.pb.cc"
         "${PROTO_GEN_DIR}/proto/file_transfer.trpc.pb.h"
   COMMAND ${PB_PROTOC}
          --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
          --cpp_out=${PROTO_GEN_DIR}
          --plugin=protoc-gen-trpc=${TRPC_CPP_PLUGIN}
          --trpc_out=${PROTO_GEN_DIR}
          ${PROTO_FILES}
  DEPENDS ${PROTO_FILES}
  COMMENT "从 .proto 文件生成 C++ 源代码"
)

# 创建一个自定义目标来触发代码生成
add_custom_target(generate_proto ALL
  DEPENDS "${PROTO_GEN_DIR}/proto/file_transfer.pb.cc"
          "${PROTO_GEN_DIR}/proto/file_transfer.pb.h"
          "${PROTO_GEN_DIR}/proto/file_transfer.trpc.pb.cc"
          "${PROTO_GEN_DIR}/proto/file_transfer.trpc.pb.h"
)

# 包含生成的头文件目录
include_directories(${PROTO_GEN_DIR})

# ----------------- 服务端可执行文件 -----------------
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/server)
add_executable(file_transfer_server
  server/main.cc
  server/file_transfer_service.cc
  server/file_transfer_service.h
  ${PROTO_GEN_DIR}/proto/file_transfer.pb.cc
  ${PROTO_GEN_DIR}/proto/file_transfer.trpc.pb.cc
)
add_dependencies(file_transfer_server generate_proto)
target_link_libraries(file_transfer_server PRIVATE trpc ${LIBS_BASIC})


# ----------------- 客户端可执行文件 -----------------
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/client)
add_executable(file_transfer_client
  client/main.cc
  #client/file_transfer_client.cc
  #client/file_transfer_client.h
  ${PROTO_GEN_DIR}/proto/file_transfer.pb.cc
  ${PROTO_GEN_DIR}/proto/file_transfer.trpc.pb.cc
)
add_dependencies(file_transfer_client generate_proto)
target_link_libraries(file_transfer_client PRIVATE trpc ${LIBS_BASIC})

# ----------------- 安装 -----------------
# 指定安装目标
install(TARGETS file_transfer_server file_transfer_client RUNTIME DESTINATION bin)
# 安装配置文件和示例文本
install(FILES
    server/trpc_server_config.yaml
    client/trpc_client_config.yaml
    sample.txt
    DESTINATION bin
)
