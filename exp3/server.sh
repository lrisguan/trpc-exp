#!/bin/bash

# 检查参数是否正确
if [ $# -ne 1 ]; then
    echo "用法：$0 <端口号>"
    echo "示例：$0 24868  或  $0 24859"
    exit 1
fi

# 接收端口参数
PORT=$1

# 定义模板文件和临时配置文件路径
TEMPLATE_FILE="./server/trpc_cpp_fiber_template.yaml"
TEMP_CONFIG_FILE="./server/trpc_cpp_fiber_${PORT}.yaml"

# 检查模板文件是否存在
if [ ! -f "$TEMPLATE_FILE" ]; then
    echo "错误：模板文件 $TEMPLATE_FILE 不存在！"
    exit 1
fi

# 替换占位符生成临时配置文件（支持Linux/macOS）
if [ "$(uname)" = "Darwin" ]; then
    # macOS 使用 sed -i ''
    sed -i '' "s/{{PORT}}/$PORT/g" "$TEMPLATE_FILE" > "$TEMP_CONFIG_FILE"
else
    # Linux 使用 sed -i
    sed "s/{{PORT}}/$PORT/g" "$TEMPLATE_FILE" > "$TEMP_CONFIG_FILE"
fi

# 检查临时文件生成是否成功
if [ ! -f "$TEMP_CONFIG_FILE" ]; then
    echo "错误：生成临时配置文件失败！"
    exit 1
fi

echo "✅ 已生成临时配置文件：$TEMP_CONFIG_FILE（端口：$PORT）"

# 运行程序
echo "🚀 启动服务（端口：$PORT）..."
./build/http_upload_download_server \
    --download_src_path="download_src.bin" \
    --upload_dst_path="upload_dst.bin" \
    --config="$TEMP_CONFIG_FILE"

# 程序退出后，可选删除临时配置文件（注释掉则保留）
rm -f "$TEMP_CONFIG_FILE"
echo "🗑️  已删除临时配置文件：$TEMP_CONFIG_FILE