#!/bin/bash

# check arguments count
if [ $# -ne 1 ]; then
    echo "usage: $0 <port>"
    echo "examples: $0 24868  or  $0 24859"
    exit 1
fi

# Receive port argument
PORT=$1

# Define template file and temporary config file paths
TEMPLATE_FILE="./server/trpc_cpp_fiber_template.yaml"
TEMP_CONFIG_FILE="./server/trpc_cpp_fiber_${PORT}.yaml"

# check if template file exists
if [ ! -f "$TEMPLATE_FILE" ]; then
    echo "Error: Template file $TEMPLATE_FILE does not exist!"
    exit 1
fi

# Replace placeholder to generate temporary config file (support Linux/macOS)
if [ "$(uname)" = "Darwin" ]; then
    # on macOS, sed requires an argument for -i (even if it's empty)
    sed -i '' "s/{{PORT}}/$PORT/g" "$TEMPLATE_FILE" > "$TEMP_CONFIG_FILE"
else
    # on Linux, sed works directly
    sed "s/{{PORT}}/$PORT/g" "$TEMPLATE_FILE" > "$TEMP_CONFIG_FILE"
fi

# check if temporary config file was created successfully
if [ ! -f "$TEMP_CONFIG_FILE" ]; then
    echo "Error: Failed to generate temporary config file!"
    exit 1
fi

echo "✅ Temporary config file generated: $TEMP_CONFIG_FILE (Port: $PORT)"

# Start the server with the generated config file
echo "🚀 starting server on port $PORT..."
./build/http_upload_download_server \
    --download_src_path="download_src.bin" \
    --upload_dst_path="upload_dst.bin" \
    --config="$TEMP_CONFIG_FILE"

# after the program exits, optionally delete the temporary config file (comment out to keep)
rm -f "$TEMP_CONFIG_FILE"
echo "🗑️  Temporary config file deleted: $TEMP_CONFIG_FILE