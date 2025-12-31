#!/bin/bash

# Simple helper for dataset operations.
# Usage examples:
#   ./dataset_client.sh upload dataset1 big.txt 4   # 上传并分片
#   ./dataset_client.sh wc     dataset1 4           # 对 dataset1 做 WordCount

if [ $# -lt 3 ]; then
  echo "Usage: $0 upload <dataset> <file> <shards>"
  echo "       $0 wc     <dataset> <shards>"
  exit 1
fi

OP=$1
DATASET=$2

if [ "$OP" = "upload" ]; then
  if [ $# -lt 4 ]; then
    echo "Usage: $0 upload <dataset> <file> <shards>"
    exit 1
  fi
  FILE=$3
  SHARDS=$4
  ./build/dataset_client \
    --op=upload_dataset \
    --dataset="$DATASET" \
    --file="$FILE" \
    --shards="$SHARDS" \
    --client_config=./client/trpc_cpp_fiber.yaml
elif [ "$OP" = "wc" ]; then
  SHARDS=$3
  ./build/dataset_client \
    --op=wordcount_dataset \
    --dataset="$DATASET" \
    --shards="$SHARDS" \
    --wordcount_output="${DATASET}_wordcount.txt" \
    --client_config=./client/trpc_cpp_fiber.yaml
else
  echo "Unknown op: $OP (expected upload|wc)"
  exit 1
fi
