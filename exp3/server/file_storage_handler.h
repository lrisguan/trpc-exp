//
//
// Tencent is pleased to support the open source community by making tRPC available.
//
// Copyright (C) 2023 THL A29 Limited, a Tencent company.
// All rights reserved.
//
// If you have downloaded a copy of the tRPC source code from Tencent,
// please note that tRPC source code is licensed under the  Apache 2.0 License,
// A copy of the Apache 2.0 License is included in this file.
//
//

#include "trpc/util/http/stream/http_stream_handler.h"

namespace http::demo {

class FileStorageHandler : public ::trpc::http::HttpStreamHandler {
 public:
  FileStorageHandler(std::string dst, std::string src)
      : upload_dst_path_(std::move(dst)), download_src_path_(std::move(src)) {}

  ~FileStorageHandler() override = default;

  // Provides file downloading.
  ::trpc::Status Get(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                     ::trpc::http::Response* rsp) override;

  // Provides file uploading.
  ::trpc::Status Post(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                      ::trpc::http::Response* rsp) override;

 private:
  // Path-based handler for Map-Reduce wordcount map phase.
  ::trpc::Status HandleWordCountMap(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                                    ::trpc::http::Response* rsp);

  // DHT-style helpers: shard upload/download and local wordcount by key stored in header.
  ::trpc::Status HandleShardUpload(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                                   ::trpc::http::Response* rsp);

  ::trpc::Status HandleShardDownload(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                                     ::trpc::http::Response* rsp);

  ::trpc::Status HandleWordCountLocal(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                                      ::trpc::http::Response* rsp);

  // Read shard key from header (X-Shard-Key). Empty means missing.
  std::string GetShardKey(const ::trpc::http::RequestPtr& req) const;

  std::string upload_dst_path_{"upload_dst.bin"};
  std::string download_src_path_{"download_src.bin"};
};

}  // namespace http::demo
