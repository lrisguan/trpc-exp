// gateway/http_gateway_server.cc
#include <memory>
#include <string>
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
    // initialize backend servers
    InitBackendServers();
    // init hash key callback(select by flag: port or ip)
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
    // parse backend servers from flag
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

    // add to consistent hash ring
    for (const auto& server : servers) {
      consistent_hash_.AddNode(server);
    }
  }

  // process and forward(support GET/POST)
  ::trpc::Status ProcessAndForward(const ::trpc::ServerContextPtr& ctx,
                                  const ::trpc::http::RequestPtr& req,
                                  ::trpc::http::Response* rsp,
                                  ::trpc::http::MethodType method) {
    // aquire hash key via callback
    std::string hash_key = hash_key_callback_(req, ctx);
    if (hash_key.empty()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kInternalServerError);
      rsp->SetContent("Invalid hash key");
      return ::trpc::kSuccStatus;
    }

    // locate target server
    std::string target_server = consistent_hash_.GetNode(hash_key);
    if (target_server.empty()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kServiceUnavailable);
      rsp->SetContent("No available backend server");
      return ::trpc::kSuccStatus;
    }

     // add routing log
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
    // create backend service proxy
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
    // add debug log
    if (!proxy) {
      TRPC_FMT_ERROR("Failed to create proxy for backend: {}", target_server);
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      rsp->SetContent("Proxy creation failed");
      return ::trpc::kSuccStatus;
    }

    // create client context
    auto client_ctx = ::trpc::MakeClientContext(proxy);
    client_ctx->SetTimeout(5000);

    // copy request headers
    for (const auto& p : req->GetHeader().Pairs()) {
      client_ctx->SetHttpHeader(std::string(p.first), std::string(p.second));
    }

    // handle GET and POST methods
    std::string path = req->GetUrl();
    auto stream = (method == ::trpc::http::MethodType::GET) ? proxy->Get(client_ctx, path)
                                                             : proxy->Post(client_ctx, path);

    if (!stream.GetStatus().OK()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      rsp->SetContent("Failed to connect to backend server");
      return ::trpc::kSuccStatus;
    }
    // transfer request body (streaming)
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
        // read error
        rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
        rsp->SetContent("Failed to read request body");
        return ::trpc::kSuccStatus;
      }
    }

    // read response headers
    int http_status = 0;
    ::trpc::http::HttpHeader header;
    auto status = stream.ReadHeaders(http_status, header);
    if (!status.OK()) {
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      return ::trpc::kSuccStatus;
    }

    // set response headers and enable streaming response
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

    // read & write response body (streaming) to original client
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
      // read error
      rsp->SetStatus(::trpc::http::ResponseStatus::kBadGateway);
      return ::trpc::kSuccStatus;
    }

    // add success log before the final return
    TRPC_FMT_INFO("✅ Successfully forwarded to backend: {} | Response status: {}", 
                  target_server, 
                  static_cast<int>(rsp->GetStatus()));
                  
    return ::trpc::kSuccStatus;
  }

  ConsistentHash consistent_hash_;
  HashKeyCallback hash_key_callback_;
};

class GatewayServer : public ::trpc::TrpcApp {
 public:
  int Initialize() override {
    auto http_service = std::make_shared<::trpc::HttpService>();
    
    // set routes
    http_service->SetRoutes([](::trpc::http::HttpRoutes& routes) {
      auto handler = std::make_shared<GatewayHandler>();
      routes.Add(::trpc::http::MethodType::GET, ::trpc::http::Path("/download"), handler);
      routes.Add(::trpc::http::MethodType::POST, ::trpc::http::Path("/upload"), handler);
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