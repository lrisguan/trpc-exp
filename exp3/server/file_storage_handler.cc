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

#include "file_storage_handler.h"
#include <filesystem>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace http::demo {

// Provides file downloading or DHT-style operations depending on path.
::trpc::Status FileStorageHandler::Get(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                                       ::trpc::http::Response* rsp) {
  // DHT-style shard download: GET /download_shard with X-Shard-Key header.
  if (req && req->GetUrl() == "/download_shard") {
    return HandleShardDownload(ctx, req, rsp);
  }

  // Local wordcount on a stored shard: GET /wordcount_local with X-Shard-Key header.
  if (req && req->GetUrl() == "/wordcount_local") {
    return HandleWordCountLocal(ctx, req, rsp);
  }

  auto fin = std::ifstream(download_src_path_, std::ios::binary);
  if (!fin.is_open()) {
    TRPC_FMT_ERROR("failed to open file: {}", download_src_path_);
    rsp->SetStatus(::trpc::http::ResponseStatus::kInternalServerError);
    return ::trpc::kSuccStatus;
  }

  // Send response content in chunked.
  rsp->SetHeader(::trpc::http::kHeaderTransferEncoding, ::trpc::http::kTransferEncodingChunked);
  auto& writer = rsp->GetStream();
  ::trpc::Status status = writer.WriteHeader();
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to send response header: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }

  std::size_t nwrite{0};
  ::trpc::BufferBuilder buffer_builder;
  for (;;) {
    fin.read(buffer_builder.data(), buffer_builder.SizeAvailable());
    std::size_t n = fin.gcount();
    if (n > 0) {
      ::trpc::NoncontiguousBuffer buffer;
      buffer.Append(buffer_builder.Seal(n));
      status = writer.Write(std::move(buffer));
      if (status.OK()) {
        nwrite += n;
        continue;
      }
      TRPC_FMT_ERROR("failed to write content: {}", status.ToString());
      return ::trpc::kStreamRstStatus;
    } else if (fin.eof()) {
      status = writer.WriteDone();
      if (status.OK()) break;
      TRPC_FMT_ERROR("failed to send write-done: {}", status.ToString());
      return ::trpc::kStreamRstStatus;
    }
    TRPC_FMT_ERROR("failed to read file");
    return ::trpc::kStreamRstStatus;
  }
  TRPC_FMT_INFO("finish providing file, write size: {}", nwrite);
  return ::trpc::kSuccStatus;
}

// Provides file uploading or wordcount map / shard upload, depending on path.
::trpc::Status FileStorageHandler::Post(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                                        ::trpc::http::Response* rsp) {
  // If the request path is for wordcount map, handle specially.
  if (req && req->GetUrl() == "/wordcount/map") {
    return HandleWordCountMap(ctx, req, rsp);
  }

   // DHT-style shard upload: POST /upload_shard with X-Shard-Key header.
  if (req && req->GetUrl() == "/upload_shard") {
    return HandleShardUpload(ctx, req, rsp);
  }

  if (req->HasHeader(::trpc::http::kHeaderContentLength)) {
    TRPC_FMT_DEBUG("the request has Content-Length: {}", req->GetHeader(::trpc::http::kHeaderContentLength));
  } else {
    TRPC_FMT_DEBUG("the request has no Content-Length, may be chunked");
  }

  auto fout = std::ofstream(upload_dst_path_, std::ios::binary);
  if (!fout.is_open()) {
    TRPC_FMT_ERROR("failed to open file: {}", download_src_path_);
    rsp->SetStatus(::trpc::http::ResponseStatus::kInternalServerError);
    return ::trpc::kSuccStatus;
  }

  ::trpc::Status status;
  auto& reader = req->GetStream();
  constexpr std::size_t kBufferSize{1024 * 1024};
  std::size_t nread{0};
  for (;;) {
    ::trpc::NoncontiguousBuffer buffer;
    status = reader.Read(buffer, kBufferSize);
    if (status.OK()) {
      nread += buffer.ByteSize();
      for (const auto& block : buffer) {
        fout.write(block.data(), block.size());
      }
      continue;
    } else if (status.StreamEof()) {
      break;
    }
    TRPC_FMT_ERROR("failed to read request content: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }

  rsp->SetStatus(::trpc::http::ResponseStatus::kOk);
  auto& writer = rsp->GetStream();
  status = writer.WriteHeader();
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to send response header: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }
  TRPC_FMT_INFO("finish storing the file, read size: {}", nread);
  return ::trpc::kSuccStatus;
}

::trpc::Status FileStorageHandler::HandleWordCountMap(const ::trpc::ServerContextPtr& ctx,
                                                      const ::trpc::http::RequestPtr& req,
                                                      ::trpc::http::Response* rsp) {
  (void)ctx;

  // Read the complete request body into memory as text.
  auto& reader = req->GetStream();
  constexpr std::size_t kBufferSize{1024 * 1024};
  ::trpc::Status status;
  std::string content;
  for (;;) {
    ::trpc::NoncontiguousBuffer buffer;
    status = reader.Read(buffer, kBufferSize);
    if (status.OK()) {
      for (const auto& block : buffer) {
        content.append(block.data(), block.size());
      }
      continue;
    } else if (status.StreamEof()) {
      break;
    }
    TRPC_FMT_ERROR("failed to read request content for wordcount map: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }

  // Simple word-splitting: sequences of [0-9A-Za-z] are words, other chars are separators.
  std::unordered_map<std::string, std::uint64_t> word_counts;
  std::string current;
  current.reserve(64);
  for (unsigned char ch : content) {
    if (std::isalnum(ch)) {
      current.push_back(static_cast<char>(std::tolower(ch)));
    } else if (!current.empty()) {
      ++word_counts[current];
      current.clear();
    }
  }
  if (!current.empty()) {
    ++word_counts[current];
  }

  // Serialize partial result as lines: "word count\n".
  std::ostringstream oss;
  for (const auto& kv : word_counts) {
    oss << kv.first << ' ' << kv.second << '\n';
  }
  std::string result = oss.str();

  rsp->SetStatus(::trpc::http::ResponseStatus::kOk);
  rsp->AddHeader("Content-Type", "text/plain; charset=utf-8");
  rsp->SetContent(result);

  TRPC_FMT_INFO("wordcount map finished, words: {}", word_counts.size());
  return ::trpc::kSuccStatus;
}

// Basic helper: get shard key from header.
std::string FileStorageHandler::GetShardKey(const ::trpc::http::RequestPtr& req) const {
  if (!req) return {};
  constexpr char kHeaderName[] = "X-Shard-Key";
  if (!req->HasHeader(kHeaderName)) return {};
  return req->GetHeader(kHeaderName);
}

::trpc::Status FileStorageHandler::HandleShardUpload(const ::trpc::ServerContextPtr& ctx,
                                                     const ::trpc::http::RequestPtr& req,
                                                     ::trpc::http::Response* rsp) {
  (void)ctx;

  std::string key = GetShardKey(req);
  if (key.empty()) {
    rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
    rsp->SetContent("Missing X-Shard-Key header");
    return ::trpc::kSuccStatus;
  }

  const std::string shard_dir = "dataset";
  std::error_code ec;
  std::filesystem::create_directories(shard_dir, ec);
  if (ec) {
    TRPC_FMT_ERROR("failed to create shard directory {}: {}", shard_dir, ec.message());
    rsp->SetStatus(::trpc::http::ResponseStatus::kInternalServerError);
    return ::trpc::kSuccStatus;
  }

  const std::string shard_path = shard_dir + "/" + key;
  auto fout = std::ofstream(shard_path, std::ios::binary);
  if (!fout.is_open()) {
    TRPC_FMT_ERROR("failed to open shard file: {}", shard_path);
    rsp->SetStatus(::trpc::http::ResponseStatus::kInternalServerError);
    return ::trpc::kSuccStatus;
  }

  ::trpc::Status status;
  auto& reader = req->GetStream();
  constexpr std::size_t kBufferSize{1024 * 1024};
  std::size_t nread{0};
  for (;;) {
    ::trpc::NoncontiguousBuffer buffer;
    status = reader.Read(buffer, kBufferSize);
    if (status.OK()) {
      nread += buffer.ByteSize();
      for (const auto& block : buffer) {
        fout.write(block.data(), block.size());
      }
      continue;
    } else if (status.StreamEof()) {
      break;
    }
    TRPC_FMT_ERROR("failed to read request content for shard upload: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }

  rsp->SetStatus(::trpc::http::ResponseStatus::kOk);
  auto& writer = rsp->GetStream();
  status = writer.WriteHeader();
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to send response header for shard upload: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }

  TRPC_FMT_INFO("finish shard upload, key: {}, path: {}, size: {}", key, shard_path, nread);
  return ::trpc::kSuccStatus;
}

::trpc::Status FileStorageHandler::HandleShardDownload(const ::trpc::ServerContextPtr& ctx,
                                                       const ::trpc::http::RequestPtr& req,
                                                       ::trpc::http::Response* rsp) {
  (void)ctx;

  std::string key = GetShardKey(req);
  if (key.empty()) {
    rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
    rsp->SetContent("Missing X-Shard-Key header");
    return ::trpc::kSuccStatus;
  }

  const std::string shard_path = std::string("dataset/") + key;
  auto fin = std::ifstream(shard_path, std::ios::binary);
  if (!fin.is_open()) {
    TRPC_FMT_ERROR("failed to open shard file: {}", shard_path);
    rsp->SetStatus(::trpc::http::ResponseStatus::kNotFound);
    return ::trpc::kSuccStatus;
  }

  rsp->SetHeader(::trpc::http::kHeaderTransferEncoding, ::trpc::http::kTransferEncodingChunked);
  auto& writer = rsp->GetStream();
  ::trpc::Status status = writer.WriteHeader();
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to send response header for shard download: {}", status.ToString());
    return ::trpc::kStreamRstStatus;
  }

  std::size_t nwrite{0};
  ::trpc::BufferBuilder buffer_builder;
  for (;;) {
    fin.read(buffer_builder.data(), buffer_builder.SizeAvailable());
    std::size_t n = fin.gcount();
    if (n > 0) {
      ::trpc::NoncontiguousBuffer buffer;
      buffer.Append(buffer_builder.Seal(n));
      status = writer.Write(std::move(buffer));
      if (status.OK()) {
        nwrite += n;
        continue;
      }
      TRPC_FMT_ERROR("failed to write shard content: {}", status.ToString());
      return ::trpc::kStreamRstStatus;
    } else if (fin.eof()) {
      status = writer.WriteDone();
      if (status.OK()) break;
      TRPC_FMT_ERROR("failed to send write-done for shard download: {}", status.ToString());
      return ::trpc::kStreamRstStatus;
    }
    TRPC_FMT_ERROR("failed to read shard file: {}", shard_path);
    return ::trpc::kStreamRstStatus;
  }

  TRPC_FMT_INFO("finish shard download, key: {}, size: {}", key, nwrite);
  return ::trpc::kSuccStatus;
}

::trpc::Status FileStorageHandler::HandleWordCountLocal(const ::trpc::ServerContextPtr& ctx,
                                                        const ::trpc::http::RequestPtr& req,
                                                        ::trpc::http::Response* rsp) {
  (void)ctx;

  std::string key = GetShardKey(req);
  if (key.empty()) {
    rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
    rsp->SetContent("Missing X-Shard-Key header");
    return ::trpc::kSuccStatus;
  }

  const std::string shard_path = std::string("dataset/") + key;
  auto fin = std::ifstream(shard_path, std::ios::binary);
  if (!fin.is_open()) {
    TRPC_FMT_ERROR("failed to open shard file for local wordcount: {}", shard_path);
    rsp->SetStatus(::trpc::http::ResponseStatus::kNotFound);
    return ::trpc::kSuccStatus;
  }

  // Read entire shard content into memory.
  std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());

  std::unordered_map<std::string, std::uint64_t> word_counts;
  std::string current;
  current.reserve(64);
  for (unsigned char ch : content) {
    if (std::isalnum(ch)) {
      current.push_back(static_cast<char>(std::tolower(ch)));
    } else if (!current.empty()) {
      ++word_counts[current];
      current.clear();
    }
  }
  if (!current.empty()) {
    ++word_counts[current];
  }

  std::ostringstream oss;
  for (const auto& kv : word_counts) {
    oss << kv.first << ' ' << kv.second << '\n';
  }

  rsp->SetStatus(::trpc::http::ResponseStatus::kOk);
  rsp->AddHeader("Content-Type", "text/plain; charset=utf-8");
  rsp->SetContent(oss.str());

  TRPC_FMT_INFO("local wordcount finished, key: {}, words: {}", key, word_counts.size());
  return ::trpc::kSuccStatus;
}

}  // namespace http::demo
