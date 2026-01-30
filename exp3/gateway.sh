#!/bin/bash
if [ $# -lt 1 ]; then
    echo "用法：$0 <后端端口1> [后端端口2 ...]"
    echo "示例：启动单个后端：$0 24858"
    echo "      启动多个后端：$0 24858 24859 24868"
    exit 1
fi

# 拼接后端服务列表（默认IP为127.0.0.1）
BACKEND_SERVERS=""
for PORT in "$@"; do
    if [ -z "$BACKEND_SERVERS" ]; then
        BACKEND_SERVERS="127.0.0.1:$PORT"
    else
        BACKEND_SERVERS="$BACKEND_SERVERS,127.0.0.1:$PORT"
    fi
done
echo "✅ 后端服务列表：$BACKEND_SERVERS"
./build/http_gateway --config=./gateway/trpc_cpp_fiber.yaml --backend_servers="$BACKEND_SERVERS"