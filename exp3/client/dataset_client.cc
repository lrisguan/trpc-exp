// Dataset-oriented DHT storage and MapReduce client.
// Provides two operations:
//  1) upload_dataset: split a large file into N shards and upload via gateway /upload_shard
//  2) wordcount_dataset: trigger dataset-based MapReduce via gateway /wordcount_dataset

#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>

#include "gflags/gflags.h"

#include "trpc/client/http/http_service_proxy.h"
#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/config/trpc_config.h"
#include "trpc/common/runtime_manager.h"
#include "trpc/coroutine/fiber.h"
#include "trpc/coroutine/fiber_latch.h"
#include "trpc/util/log/logging.h"

DEFINE_string(service_name, "http_gateway_service", "callee service name");
DEFINE_string(client_config, "trpc_cpp_fiber.yaml", "");
DEFINE_string(addr, "127.0.0.1:24860", "ip:port of gateway");
DEFINE_string(op, "upload_dataset", "operation: upload_dataset | wordcount_dataset");
DEFINE_string(dataset, "dataset1", "dataset name prefix");
DEFINE_string(file, "dataset.txt", "local dataset file path");
DEFINE_int32(shards, 4, "number of shards to split dataset into");
DEFINE_string(wordcount_output, "dataset_wordcount.txt", "wordcount result output file");

namespace http::demo {
using HttpServiceProxyPtr = std::shared_ptr<::trpc::http::HttpServiceProxy>;

bool UploadShard(const HttpServiceProxyPtr& proxy, const std::string& dataset,
                 int shard_index, const std::string& content) {
  std::string shard_key = dataset + "_" + std::to_string(shard_index);

  auto ctx = ::trpc::MakeClientContext(proxy);
  ctx->SetTimeout(5000);
  ctx->SetHttpHeader("X-Shard-Key", shard_key);
  ctx->SetHttpHeader(::trpc::http::kHeaderTransferEncoding, ::trpc::http::kTransferEncodingChunked);

  auto stream = proxy->Post(ctx, "/upload_shard");
  if (!stream.GetStatus().OK()) {
    TRPC_FMT_ERROR("failed to create client stream for upload_shard, key: {}", shard_key);
    return false;
  }

  std::istringstream in(content);
  ::trpc::BufferBuilder buffer_builder;
  for (;;) {
    ::trpc::Status status;
    in.read(buffer_builder.data(), buffer_builder.SizeAvailable());
    std::size_t n = static_cast<std::size_t>(in.gcount());
    if (n > 0) {
      ::trpc::NoncontiguousBuffer buffer;
      buffer.Append(buffer_builder.Seal(n));
      status = stream.Write(std::move(buffer));
      if (status.OK()) {
        continue;
      }
      TRPC_FMT_ERROR("failed to write shard content key {}: {}", shard_key, status.ToString());
      return false;
    } else if (in.eof()) {
      status = stream.WriteDone();
      if (status.OK()) break;
      TRPC_FMT_ERROR("failed to send write-done for shard key {}: {}", shard_key, status.ToString());
      return false;
    }
    TRPC_FMT_ERROR("failed to read shard stream for key {}", shard_key);
    return false;
  }

  int http_status = 0;
  ::trpc::http::HttpHeader http_header;
  ::trpc::Status status = stream.ReadHeaders(http_status, http_header);
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to read http header for upload_shard key {}: {}", shard_key, status.ToString());
    return false;
  }
  if (http_status != ::trpc::http::ResponseStatus::kOk) {
    TRPC_FMT_ERROR("upload_shard http status for key {}: {}", shard_key, http_status);
    return false;
  }

  TRPC_FMT_INFO("upload_shard success, key: {}", shard_key);
  return true;
}

bool UploadDataset(const HttpServiceProxyPtr& proxy, const std::string& dataset,
                   const std::string& file_path, int shards) {
  auto fin = std::ifstream(file_path, std::ios::binary);
  if (!fin.is_open()) {
    TRPC_FMT_ERROR("failed to open dataset file: {}", file_path);
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());

  if (shards <= 0) {
    TRPC_FMT_ERROR("invalid shard count: {}", shards);
    return false;
  }

  std::vector<std::string> shard_contents(shards);
  // 按“完整单词”轮询分配到各 shard，避免在字节边界截断单词，
  // 使得分布式 wordcount 与单机版本严格一致。
  if (!content.empty()) {
    std::string current;
    current.reserve(64);
    int shard_index = 0;

    auto flush_word = [&]() {
      if (current.empty()) return;
      std::string& out = shard_contents[shard_index];
      if (!out.empty()) out.push_back(' ');
      out.append(current);
      current.clear();
      shard_index = (shard_index + 1) % shards;
    };

    for (unsigned char ch : content) {
      if (std::isalnum(ch)) {
        current.push_back(static_cast<char>(std::tolower(ch)));
      } else {
        flush_word();
      }
    }
    flush_word();
  }

  // 将分片后的内容写入本地 dataset 目录，便于查看与调试
  const std::string shard_dir = "dataset";
  std::error_code ec;
  std::filesystem::create_directories(shard_dir, ec);
  if (ec) {
    TRPC_FMT_ERROR("failed to create shard directory {}: {}", shard_dir, ec.message());
    return false;
  }

  for (int i = 0; i < shards; ++i) {
    const std::string shard_path = shard_dir + "/" + dataset + "_" + std::to_string(i);
    std::ofstream shard_out(shard_path, std::ios::binary);
    if (!shard_out.is_open()) {
      TRPC_FMT_ERROR("failed to open shard file: {}", shard_path);
      return false;
    }
    shard_out.write(shard_contents[i].data(), static_cast<std::streamsize>(shard_contents[i].size()));
    if (!shard_out.good()) {
      TRPC_FMT_ERROR("failed to write shard file: {}", shard_path);
      return false;
    }
  }

  bool ok = true;
  for (int i = 0; i < shards; ++i) {
    ok &= UploadShard(proxy, dataset, i, shard_contents[i]);
  }
  return ok;
}

bool WordCountDataset(const HttpServiceProxyPtr& proxy, const std::string& dataset,
                      int shards, const std::string& output_path) {
  auto ctx = ::trpc::MakeClientContext(proxy);
  ctx->SetTimeout(10000);
  ctx->SetHttpHeader("X-Dataset-Key", dataset);
  ctx->SetHttpHeader("X-Shards", std::to_string(shards));

  auto stream = proxy->Get(ctx, "/wordcount_dataset");
  if (!stream.GetStatus().OK()) {
    TRPC_FMT_ERROR("failed to create client stream for wordcount_dataset");
    return false;
  }

  int http_status = 0;
  ::trpc::http::HttpHeader http_header;
  ::trpc::Status status = stream.ReadHeaders(http_status, http_header);
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to read http header for wordcount_dataset: {}", status.ToString());
    return false;
  }
  if (http_status != ::trpc::http::ResponseStatus::kOk) {
    TRPC_FMT_ERROR("wordcount_dataset http status: {}", http_status);
    return false;
  }

  auto fout = std::ofstream(output_path, std::ios::binary);
  if (!fout.is_open()) {
    TRPC_FMT_ERROR("failed to open output file for wordcount_dataset: {}", output_path);
    return false;
  }

  constexpr std::size_t kBufferSize{1024 * 1024};
  for (;;) {
    ::trpc::NoncontiguousBuffer buffer;
    status = stream.Read(buffer, kBufferSize);
    if (status.OK()) {
      for (const auto& block : buffer) {
        fout.write(block.data(), block.size());
      }
      continue;
    } else if (status.StreamEof()) {
      break;
    }
    TRPC_FMT_ERROR("failed to read response content for wordcount_dataset: {}", status.ToString());
    return false;
  }

  TRPC_FMT_INFO("wordcount_dataset finished, dataset: {}, shards: {}, output: {}",
                dataset, shards, output_path);
  return true;
}

int Run() {
  bool final_ok{true};

  ::trpc::ServiceProxyOption option;
  option.name = FLAGS_service_name;
  option.codec_name = "http";
  option.network = "tcp";
  option.conn_type = "long";
  option.timeout = 5000;
  option.selector_name = "direct";
  option.target = FLAGS_addr;

  auto http_client = ::trpc::GetTrpcClient()->GetProxy<::trpc::http::HttpServiceProxy>(FLAGS_service_name, option);
  if (!http_client) {
    TRPC_FMT_ERROR("failed to create http service proxy");
    return -1;
  }

  if (FLAGS_op == "upload_dataset") {
    final_ok = UploadDataset(http_client, FLAGS_dataset, FLAGS_file, FLAGS_shards);
  } else if (FLAGS_op == "wordcount_dataset") {
    final_ok = WordCountDataset(http_client, FLAGS_dataset, FLAGS_shards, FLAGS_wordcount_output);
  } else {
    TRPC_FMT_ERROR("unknown op: {}", FLAGS_op);
    final_ok = false;
  }

  std::cout << "final result of dataset op: " << final_ok << std::endl;
  return final_ok ? 0 : -1;
}

}  // namespace http::demo

bool ParseClientConfig(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::CommandLineFlagInfo info;
  if (GetCommandLineFlagInfo("client_config", &info) && info.is_default) {
    std::cerr << "start client with client_config, for example: " << argv[0]
              << " --client_config=/client/client_config/filepath" << std::endl;
    return false;
  }
  std::cout << "FLAGS_service_name: " << FLAGS_service_name << std::endl;
  std::cout << "FLAGS_client_config: " << FLAGS_client_config << std::endl;
  std::cout << "FLAGS_addr: " << FLAGS_addr << std::endl;
  std::cout << "FLAGS_op: " << FLAGS_op << std::endl;
  std::cout << "FLAGS_dataset: " << FLAGS_dataset << std::endl;
  std::cout << "FLAGS_file: " << FLAGS_file << std::endl;
  std::cout << "FLAGS_shards: " << FLAGS_shards << std::endl;
  std::cout << "FLAGS_wordcount_output: " << FLAGS_wordcount_output << std::endl;
  return true;
}

int main(int argc, char* argv[]) {
  if (!ParseClientConfig(argc, argv)) {
    exit(-1);
  }

  if (::trpc::TrpcConfig::GetInstance()->Init(FLAGS_client_config) != 0) {
    std::cerr << "load client_config failed." << std::endl;
    exit(-1);
  }

  return ::trpc::RunInTrpcRuntime([]() { return http::demo::Run(); });
}
