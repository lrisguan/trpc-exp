#!/bin/bash
if [ $# -lt 1 ]; then
    echo "usage: $0 <backend port 1> [backend port 2 ...]"
    echo "examples: register single backend:$0 24858"
    echo "          register mutiple backends:$0 24858 24859 24868"
    exit 1
fi

# concatenate backend server list (default IP is 127.0.0.1)
BACKEND_SERVERS=""
for PORT in "$@"; do
    if [ -z "$BACKEND_SERVERS" ]; then
        BACKEND_SERVERS="127.0.0.1:$PORT"
    else
        BACKEND_SERVERS="$BACKEND_SERVERS,127.0.0.1:$PORT"
    fi
done
echo "✅ backend servers list:$BACKEND_SERVERS"
./build/http_gateway --config=./gateway/trpc_cpp_fiber.yaml --backend_servers="$BACKEND_SERVERS"