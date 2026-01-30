#!/bin/bash

# Simple wrapper to run the wordcount client against the HTTP gateway.
# Input text file defaults to wordcount_input.txt, output to wordcount_result.txt.

./build/wordcount_client \
  --src_path="vocab.txt" \
  --dst_path="result.txt" \
  --client_config=./client/trpc_cpp_fiber.yaml
