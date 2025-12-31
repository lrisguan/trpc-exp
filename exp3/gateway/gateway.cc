// gateway/http_gateway_server.cc
#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "trpc/common/trpc_app.h"
#include "trpc/server/http_service.h"
#include "trpc/util/http/stream/http_stream_handler.h"
#include "trpc/util/http/routes.h"
#include "trpc/client/http/http_service_proxy.h"
#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "gflags/gflags.h"
#include "consistent_hash.h"

// DEFINE_string(backend_servers, "127.0.0.1:24858,127.0.0.1:24859", "backend servers list");
DEFINE_string(backend_servers, "127.0.0.1:1234,127.0.0.1:24858", "backend servers list");
DEFINE_bool(hash_by_port, true, "hash by port (true) or ip (false)");
DEFINE_string(gateway_config, "trpc_cpp_fiber.yaml", "");

namespace http::gateway {

class GatewayHandler : public ::trpc::http::HttpStreamHandler {
 public:
  using HashKeyCallback = std::function<std::string(const ::trpc::http::RequestPtr&, const ::trpc::ServerContextPtr&)>;

  GatewayHandler() {
    // 初始化后端服务器
    InitBackendServers();
    // 初始化哈希回调（默认按 flag 选择 port 或 ip）
    if (FLAGS_hash_by_port) {
      hash_key_callback_ = [](const ::trpc::http::RequestPtr& /*req*/, const ::trpc::ServerContextPtr& ctx) {
        return std::to_string(ctx->GetPort());
      };
    } else {
      hash_key_callback_ = [](const ::trpc::http::RequestPtr& /*req*/, const ::trpc::ServerContextPtr& ctx) {
        return ctx->GetIp();
      };
    }
  }

  // For stream handlers, implement Get and Post.
  ::trpc::Status Get(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                     ::trpc::http::Response* rsp) override {
    return ProcessAndForward(ctx, req, rsp, ::trpc::http::MethodType::GET);
  }

  ::trpc::Status Post(const ::trpc::ServerContextPtr& ctx, const ::trpc::http::RequestPtr& req,
                      ::trpc::http::Response* rsp) override {
    return ProcessAndForward(ctx, req, rsp, ::trpc::http::MethodType::POST);
  }

 private:
  void InitBackendServers() {
    // 解析后端服务器列表
    std::vector<std::string> servers;
    size_t pos = 0;
    std::string servers_str = FLAGS_backend_servers;
    std::cout << "Backend servers: " << servers_str << std::endl;
    while (pos < servers_str.size()) {
      size_t comma = servers_str.find(',', pos);
      if (comma == std::string::npos) {
        servers.push_back(servers_str.substr(pos));
        break;
      }
      servers.push_back(servers_str.substr(pos, comma - pos));
      pos = comma + 1;
    }

    // 添加到哈希环并记录后端列表
    for (const auto& server : servers) {
      backend_servers_.push_back(server);
      consistent_hash_.AddNode(server);
    }
  }

  // 处理并转发请求（支持 GET/POST）
  ::trpc::Status ProcessAndForward(const ::trpc::ServerContextPtr& ctx,
                                  const ::trpc::http::RequestPtr& req,
                                  ::trpc::http::Response* rsp,
                                  ::trpc::http::MethodType method) {
    // Special endpoint: Map-Reduce wordcount aggregation (body-based).
    if (method == ::trpc::http::MethodType::POST && req && req->GetUrl() == "/wordcount") {
      return HandleWordCount(ctx, req, rsp);
    }

    // Special endpoint: dataset-based MapReduce wordcount using DHT-stored shards.
    if (method == ::trpc::http::MethodType::GET && req && req->GetUrl() == "/wordcount_dataset") {
      return HandleWordCountDataset(ctx, req, rsp);
    }

    // For normal upload/download and shard operations, choose hash key.
    std::string hash_key;
    // Prefer explicit shard key header if present (DHT semantics).
    if (req && req->HasHeader("X-Shard-Key")) {
      hash_key = req->GetHeader("X-Shard-Key");
    } else {
      // Fallback: use existing hash-by-port or ip behavior.
      hash_key = hash_key_callback_(req, ctx);
    }
    if (hash_key.empty()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kInternalServerError);
      rsp->SetContent("Invalid hash key");
      return ::trpc::kSuccStatus;
    }

    // 查找目标服务器
    std::string target_server = consistent_hash_.GetNode(hash_key);
    if (target_server.empty()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kServiceUnavailable);
      rsp->SetContent("No available backend server");
      return ::trpc::kSuccStatus;
    }

     // **添加路由日志**
    TRPC_FMT_INFO("🔄 Routing {} request | Path: {} | HashKey: {} | Backend: {}", 
                  method == ::trpc::http::MethodType::GET ? "GET" : "POST",
                  req->GetUrl(), 
                  hash_key, 
                  target_server);
    return ForwardRequest(ctx, req, rsp, target_server, method);
  }

  ::trpc::Status ForwardRequest(const ::trpc::ServerContextPtr& ctx,
                               const ::trpc::http::RequestPtr& req,
                               ::trpc::http::Response* rsp,
                               const std::string& target_server,
                               ::trpc::http::MethodType method) {
    // 创建后端服务代理
    ::trpc::ServiceProxyOption option;
    option.codec_name = "http";
    option.network = "tcp";
    option.conn_type = "long";
    option.timeout = 5000;
    option.selector_name = "direct";
    option.target = target_server;
    option.threadmodel_instance_name = "fiber_instance";

    auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::http::HttpServiceProxy>(
        "http_backend_service", option);
    // 添加调试日志
    if (!proxy) {
      TRPC_FMT_ERROR("Failed to create proxy for backend: {}", target_server);
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      rsp->SetContent("Proxy creation failed");
      return ::trpc::kSuccStatus;
    }

    // 创建客户端上下文
    auto client_ctx = ::trpc::MakeClientContext(proxy);
    client_ctx->SetTimeout(5000);

    // 复制请求头
    for (const auto& p : req->GetHeader().Pairs()) {
      client_ctx->SetHttpHeader(std::string(p.first), std::string(p.second));
    }

    // 处理不同的HTTP方法
    std::string path = req->GetUrl();
    auto stream = (method == ::trpc::http::MethodType::GET) ? proxy->Get(client_ctx, path)
                                                             : proxy->Post(client_ctx, path);

    if (!stream.GetStatus().OK()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      rsp->SetContent("Failed to connect to backend server");
      return ::trpc::kSuccStatus;
    }
    // 转发请求体（流式转发）
    {
      auto& reader = req->GetStream();
      constexpr std::size_t kBufferSize{1024 * 1024};
      ::trpc::Status rstatus;
      for (;;) {
        ::trpc::NoncontiguousBuffer buffer;
        rstatus = reader.Read(buffer, kBufferSize);
        if (rstatus.OK()) {
          auto wstatus = stream.Write(std::move(buffer));
          if (!wstatus.OK()) {
            rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
            rsp->SetContent("Failed to write to backend server");
            return ::trpc::kSuccStatus;
          }
          continue;
        } else if (rstatus.StreamEof()) {
          auto wstatus = stream.WriteDone();
          if (!wstatus.OK()) {
            rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
            rsp->SetContent("Failed to finish write to backend server");
            return ::trpc::kSuccStatus;
          }
          break;
        }
        // 读取出错
        rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
        rsp->SetContent("Failed to read request body");
        return ::trpc::kSuccStatus;
      }
    }

    // 读取响应头
    int http_status = 0;
    ::trpc::http::HttpHeader header;
    auto status = stream.ReadHeaders(http_status, header);
    if (!status.OK()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      return ::trpc::kSuccStatus;
    }

    // 设置响应头并启用流式响应
    rsp->SetStatus(static_cast<::trpc::http::ResponseStatus>(http_status));
    for (const auto& kv : header.Pairs()) {
      rsp->AddHeader(std::string(kv.first), std::string(kv.second));
    }

    auto& writer = rsp->GetStream();
    status = writer.WriteHeader();
    if (!status.OK()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      return ::trpc::kSuccStatus;
    }

    // 读取后端响应体并写回给原始客户端
    constexpr std::size_t kBufferSize{1024 * 1024};
    for (;;) {
      ::trpc::NoncontiguousBuffer resp_buffer;
      status = stream.Read(resp_buffer, kBufferSize);
      if (status.OK()) {
        auto wstatus = writer.Write(std::move(resp_buffer));
        if (!wstatus.OK()) {
          rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
          return ::trpc::kSuccStatus;
        }
        continue;
      } else if (status.StreamEof()) {
        auto wstatus = writer.WriteDone();
        if (!wstatus.OK()) {
          rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
          return ::trpc::kSuccStatus;
        }
        break;
      }
      // 读取出错
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      return ::trpc::kSuccStatus;
    }

    // 在最后的 return 前添加成功日志
    TRPC_FMT_INFO("✅ Successfully forwarded to backend: {} | Response status: {}", 
                  target_server, 
                  static_cast<int>(rsp->GetStatus()));
                  
    return ::trpc::kSuccStatus;
  }

  ConsistentHash consistent_hash_;
  HashKeyCallback hash_key_callback_;
  std::vector<std::string> backend_servers_;

  // Map-Reduce aggregation for /wordcount.
  ::trpc::Status HandleWordCount(const ::trpc::ServerContextPtr& ctx,
                                 const ::trpc::http::RequestPtr& req,
                                 ::trpc::http::Response* rsp) {
    (void)ctx;

    if (backend_servers_.empty()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kServiceUnavailable);
      rsp->SetContent("No backend servers configured for wordcount");
      return ::trpc::kSuccStatus;
    }

    // Read full request body into memory.
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
      TRPC_FMT_ERROR("failed to read request content for /wordcount: {}", status.ToString());
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
      rsp->SetContent("Failed to read request body");
      return ::trpc::kSuccStatus;
    }

    // Split content into chunks by whole words (avoid cutting a word across backends).
    std::size_t n_backends = backend_servers_.size();
    std::vector<std::string> chunks(n_backends);
    if (!content.empty()) {
      std::string current;
      current.reserve(64);
      std::size_t backend_index = 0;

      auto flush_word = [&]() {
        if (current.empty()) return;
        auto& out = chunks[backend_index];
        if (!out.empty()) out.push_back(' ');
        out.append(current);
        current.clear();
        backend_index = (backend_index + 1) % n_backends;
      };

      for (unsigned char ch : content) {
        if (std::isalnum(ch)) {
          current.push_back(static_cast<char>(ch));
        } else {
          flush_word();
        }
      }
      flush_word();
    }

    std::unordered_map<std::string, std::uint64_t> total_counts;

    for (std::size_t i = 0; i < backend_servers_.size(); ++i) {
      const auto& backend = backend_servers_[i];
      const auto& chunk = chunks[i];

      if (chunk.empty()) {
        continue;
      }

      std::string partial;
      if (!CallWordCountMapOnBackend(backend, chunk, partial)) {
        TRPC_FMT_ERROR("wordcount map failed on backend: {}", backend);
        rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
        rsp->SetContent("Wordcount map failed on backend");
        return ::trpc::kSuccStatus;
      }

      MergePartialResult(partial, total_counts);
    }

    // Serialize final result as text lines: "word count\n".
    std::vector<std::pair<std::string, std::uint64_t>> items(total_counts.begin(), total_counts.end());
    // 按 count 降序，若相同则按单词字典序升序，与 single_wordcount 保持一致。
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
      if (a.second != b.second) return a.second > b.second;
      return a.first < b.first;
    });

    std::ostringstream oss;
    for (const auto& kv : items) {
      oss << kv.first << ' ' << kv.second << '\n';
    }

    rsp->SetStatus(::trpc::http::ResponseStatus::kOk);
    rsp->AddHeader("Content-Type", "text/plain; charset=utf-8");
    rsp->SetContent(oss.str());

    TRPC_FMT_INFO("wordcount aggregation finished, unique words: {}", total_counts.size());
    return ::trpc::kSuccStatus;
  }

  // Dataset-based MapReduce: GET /wordcount_dataset
  // Headers:
  //   X-Dataset-Key: dataset name prefix (e.g., dataset1)
  //   X-Shards: number of shards (e.g., 4)
  ::trpc::Status HandleWordCountDataset(const ::trpc::ServerContextPtr& ctx,
                                        const ::trpc::http::RequestPtr& req,
                                        ::trpc::http::Response* rsp) {
    (void)ctx;

    if (backend_servers_.empty()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kServiceUnavailable);
      rsp->SetContent("No backend servers configured for wordcount_dataset");
      return ::trpc::kSuccStatus;
    }

    if (!req->HasHeader("X-Dataset-Key") || !req->HasHeader("X-Shards")) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
      rsp->SetContent("Missing X-Dataset-Key or X-Shards header");
      return ::trpc::kSuccStatus;
    }

    std::string dataset = req->GetHeader("X-Dataset-Key");
    std::string shards_str = req->GetHeader("X-Shards");
    std::size_t shard_count = 0;
    try {
      shard_count = static_cast<std::size_t>(std::stoul(shards_str));
    } catch (...) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
      rsp->SetContent("Invalid X-Shards header");
      return ::trpc::kSuccStatus;
    }
    if (shard_count == 0) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadRequest);
      rsp->SetContent("X-Shards must be > 0");
      return ::trpc::kSuccStatus;
    }

    std::unordered_map<std::string, std::uint64_t> total_counts;

    // For each shard, compute key and route to its owning backend via consistent hash.
    for (std::size_t i = 0; i < shard_count; ++i) {
      std::string shard_key = dataset + "_" + std::to_string(i);
      std::string target_server = consistent_hash_.GetNode(shard_key);
      TRPC_FMT_INFO("dataset {} Routing shard {} to backend {}", i, shard_key, target_server);
      if (target_server.empty()) {
        TRPC_FMT_ERROR("no backend server for shard key: {}", shard_key);
        rsp->SetStatus(::trpc::http::ResponseStatus::kServiceUnavailable);
        rsp->SetContent("No backend server for shard key");
        return ::trpc::kSuccStatus;
      }

      std::string partial;
      if (!CallWordCountLocalOnBackend(target_server, shard_key, partial)) {
        TRPC_FMT_ERROR("wordcount_local failed for shard key: {} on backend: {}", shard_key, target_server);
        rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
        rsp->SetContent("wordcount_local failed on backend");
        return ::trpc::kSuccStatus;
      }

      MergePartialResult(partial, total_counts);
    }

    std::vector<std::pair<std::string, std::uint64_t>> items(total_counts.begin(), total_counts.end());
    // 与单机版排序规则保持一致：count 降序，相等时按单词字典序。
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
      if (a.second != b.second) return a.second > b.second;
      return a.first < b.first;
    });

    std::ostringstream oss;
    for (const auto& kv : items) {
      oss << kv.first << ' ' << kv.second << '\n';
    }

    rsp->SetStatus(::trpc::http::ResponseStatus::kOk);
    rsp->AddHeader("Content-Type", "text/plain; charset=utf-8");
    rsp->SetContent(oss.str());

    TRPC_FMT_INFO("wordcount_dataset finished, dataset: {}, shards: {}, unique words: {}",
                  dataset, shard_count, total_counts.size());
    return ::trpc::kSuccStatus;
  }

  bool CallWordCountMapOnBackend(const std::string& target_server, const std::string& chunk,
                                 std::string& partial_result) {
    ::trpc::ServiceProxyOption option;
    option.codec_name = "http";
    option.network = "tcp";
    option.conn_type = "long";
    option.timeout = 5000;
    option.selector_name = "direct";
    option.target = target_server;
    option.threadmodel_instance_name = "fiber_instance";

    auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::http::HttpServiceProxy>(
        "http_backend_service", option);
    if (!proxy) {
      TRPC_FMT_ERROR("Failed to create proxy for backend (wordcount): {}", target_server);
      return false;
    }

    auto ctx = ::trpc::MakeClientContext(proxy);
    ctx->SetTimeout(5000);
    // Use chunked transfer to send request body.
    ctx->SetHttpHeader(::trpc::http::kHeaderTransferEncoding, ::trpc::http::kTransferEncodingChunked);

    auto stream = proxy->Post(ctx, "/wordcount/map");
    if (!stream.GetStatus().OK()) {
      TRPC_FMT_ERROR("failed to create client stream for wordcount map: {}", stream.GetStatus().ToString());
      return false;
    }

    // Send chunk content.
    std::istringstream in(chunk);
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
        TRPC_FMT_ERROR("failed to write request content for wordcount map: {}", status.ToString());
        return false;
      } else if (in.eof()) {
        status = stream.WriteDone();
        if (status.OK()) break;
        TRPC_FMT_ERROR("failed to send write-done for wordcount map: {}", status.ToString());
        return false;
      }
      TRPC_FMT_ERROR("failed to read chunk for wordcount map");
      return false;
    }

    int http_status = 0;
    ::trpc::http::HttpHeader http_header;
    ::trpc::Status status = stream.ReadHeaders(http_status, http_header);
    if (!status.OK()) {
      TRPC_FMT_ERROR("failed to read http header from backend wordcount map: {}", status.ToString());
      return false;
    }
    if (http_status != ::trpc::http::ResponseStatus::kOk) {
      TRPC_FMT_ERROR("backend wordcount map http status: {}", http_status);
      return false;
    }

    constexpr std::size_t kBufferSize{1024 * 1024};
    partial_result.clear();
    for (;;) {
      ::trpc::NoncontiguousBuffer buffer;
      status = stream.Read(buffer, kBufferSize);
      if (status.OK()) {
        for (const auto& block : buffer) {
          partial_result.append(block.data(), block.size());
        }
        continue;
      } else if (status.StreamEof()) {
        break;
      }
      TRPC_FMT_ERROR("failed to read response content from backend wordcount map: {}", status.ToString());
      return false;
    }

    return true;
  }

  bool CallWordCountLocalOnBackend(const std::string& target_server, const std::string& shard_key,
                                   std::string& partial_result) {
    ::trpc::ServiceProxyOption option;
    option.codec_name = "http";
    option.network = "tcp";
    option.conn_type = "long";
    option.timeout = 5000;
    option.selector_name = "direct";
    option.target = target_server;
    option.threadmodel_instance_name = "fiber_instance";

    auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::http::HttpServiceProxy>(
        "http_backend_service", option);
    if (!proxy) {
      TRPC_FMT_ERROR("Failed to create proxy for backend (wordcount_local): {}", target_server);
      return false;
    }

    auto ctx = ::trpc::MakeClientContext(proxy);
    ctx->SetTimeout(5000);
    // Carry shard key so that backend can locate the stored file.
    ctx->SetHttpHeader("X-Shard-Key", shard_key);

    auto stream = proxy->Get(ctx, "/wordcount_local");
    if (!stream.GetStatus().OK()) {
      TRPC_FMT_ERROR("failed to create client stream for wordcount_local: {}", stream.GetStatus().ToString());
      return false;
    }

    int http_status = 0;
    ::trpc::http::HttpHeader http_header;
    ::trpc::Status status = stream.ReadHeaders(http_status, http_header);
    if (!status.OK()) {
      TRPC_FMT_ERROR("failed to read http header from backend wordcount_local: {}", status.ToString());
      return false;
    }
    if (http_status != ::trpc::http::ResponseStatus::kOk) {
      TRPC_FMT_ERROR("backend wordcount_local http status: {}", http_status);
      return false;
    }

    constexpr std::size_t kBufferSize{1024 * 1024};
    partial_result.clear();
    for (;;) {
      ::trpc::NoncontiguousBuffer buffer;
      status = stream.Read(buffer, kBufferSize);
      if (status.OK()) {
        for (const auto& block : buffer) {
          partial_result.append(block.data(), block.size());
        }
        continue;
      } else if (status.StreamEof()) {
        break;
      }
      TRPC_FMT_ERROR("failed to read response content from backend wordcount_local: {}", status.ToString());
      return false;
    }

    return true;
  }

  void MergePartialResult(const std::string& partial,
                          std::unordered_map<std::string, std::uint64_t>& total_counts) {
    std::istringstream iss(partial);
    std::string line;
    while (std::getline(iss, line)) {
      if (line.empty()) continue;
      std::istringstream ls(line);
      std::string word;
      std::uint64_t count = 0;
      if (!(ls >> word >> count)) continue;
      total_counts[word] += count;
    }
  }
};

class GatewayServer : public ::trpc::TrpcApp {
 public:
  int Initialize() override {
    auto http_service = std::make_shared<::trpc::HttpService>();
    
    // 设置路由
    http_service->SetRoutes([](::trpc::http::HttpRoutes& routes) {
      auto handler = std::make_shared<GatewayHandler>();
      routes.Add(::trpc::http::MethodType::GET, ::trpc::http::Path("/download"), handler);
      routes.Add(::trpc::http::MethodType::POST, ::trpc::http::Path("/upload"), handler);
      routes.Add(::trpc::http::MethodType::POST, ::trpc::http::Path("/wordcount"), handler);
      // DHT-style shard APIs and dataset-based MapReduce control.
      routes.Add(::trpc::http::MethodType::POST, ::trpc::http::Path("/upload_shard"), handler);
      routes.Add(::trpc::http::MethodType::GET, ::trpc::http::Path("/download_shard"), handler);
      routes.Add(::trpc::http::MethodType::GET, ::trpc::http::Path("/wordcount_dataset"), handler);
    });

    RegisterService("http_gateway_service", http_service);
    return 0;
  }

  void Destroy() override {}
};

}  // namespace http::gateway

int main(int argc, char**argv) {
  http::gateway::GatewayServer server;
  server.Main(argc, argv);
  server.Wait();
  return 0;
}