// Wordcount Map-Reduce client via HTTP gateway.
// Sends a text file to /wordcount on the gateway and saves the aggregated
// wordcount result to a local file.

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
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
DEFINE_string(src_path, "wordcount_input.txt", "input text file for wordcount");
DEFINE_string(dst_path, "wordcount_result.txt", "output file for wordcount result");

namespace http::demo {
using HttpServiceProxyPtr = std::shared_ptr<::trpc::http::HttpServiceProxy>;

bool WordCount(const HttpServiceProxyPtr& proxy, const std::string& url,
               const std::string& src_path, const std::string& dst_path) {
  auto fin = std::ifstream(src_path, std::ios::binary);
  if (!fin.is_open()) {
    TRPC_FMT_ERROR("failed to open input file, file_path: {}", src_path);
    return false;
  }

  auto ctx = ::trpc::MakeClientContext(proxy);
  ctx->SetTimeout(5000);
  // Send request content in chunked transfer encoding.
  ctx->SetHttpHeader(::trpc::http::kHeaderTransferEncoding, ::trpc::http::kTransferEncodingChunked);

  auto stream = proxy->Post(ctx, url);
  if (!stream.GetStatus().OK()) {
    TRPC_FMT_ERROR("failed to create client stream for wordcount");
    return false;
  }

  // Sends request content.
  std::size_t nwrite{0};
  ::trpc::BufferBuilder buffer_builder;
  for (;;) {
    ::trpc::Status status;
    fin.read(buffer_builder.data(), buffer_builder.SizeAvailable());
    std::size_t n = static_cast<std::size_t>(fin.gcount());
    if (n > 0) {
      ::trpc::NoncontiguousBuffer buffer;
      buffer.Append(buffer_builder.Seal(n));
      status = stream.Write(std::move(buffer));
      if (status.OK()) {
        nwrite += n;
        continue;
      }
      TRPC_FMT_ERROR("failed to write request content for wordcount: {}", status.ToString());
      return false;
    } else if (fin.eof()) {
      status = stream.WriteDone();
      if (status.OK()) break;
      TRPC_FMT_ERROR("failed to send write-done for wordcount: {}", status.ToString());
      return false;
    }
    TRPC_FMT_ERROR("failed to read input file for wordcount");
    return false;
  }

  int http_status = 0;
  ::trpc::http::HttpHeader http_header;
  ::trpc::Status status = stream.ReadHeaders(http_status, http_header);
  if (!status.OK()) {
    TRPC_FMT_ERROR("failed to read http header for wordcount: {}", status.ToString());
    return false;
  } else if (http_status != ::trpc::http::ResponseStatus::kOk) {
    TRPC_FMT_ERROR("http response status for wordcount: {}", http_status);
    return false;
  }

  auto fout = std::ofstream(dst_path, std::ios::binary);
  if (!fout.is_open()) {
    TRPC_FMT_ERROR("failed to open output file, file_path:{}", dst_path);
    return false;
  }

  constexpr std::size_t kBufferSize{1024 * 1024};
  std::size_t nread{0};

  for (;;) {
    ::trpc::NoncontiguousBuffer buffer;
    status = stream.Read(buffer, kBufferSize);
    if (status.OK()) {
      nread += buffer.ByteSize();
      for (const auto& block : buffer) {
        fout.write(block.data(), block.size());
      }
      continue;
    } else if (status.StreamEof()) {
      break;
    }
    TRPC_FMT_ERROR("failed to read response content for wordcount: {}", status.ToString());
    return false;
  }

  TRPC_FMT_INFO("finish wordcount request, sent size: {}, result size: {}", nwrite, nread);
  return true;
}

int Run() {
  bool final_ok{true};

  struct http_calling_args_t {
    std::string calling_name;
    std::function<bool()> calling_executor;
    bool ok;
  };

  ::trpc::ServiceProxyOption option;
  option.name = FLAGS_service_name;
  option.codec_name = "http";
  option.network = "tcp";
  option.conn_type = "long";
  option.timeout = 5000;
  option.selector_name = "direct";
  option.target = FLAGS_addr;

  auto http_client = ::trpc::GetTrpcClient()->GetProxy<::trpc::http::HttpServiceProxy>(FLAGS_service_name, option);
  std::vector<http_calling_args_t> callings{
      {"wordcount via gateway",
       [http_client, src_path = FLAGS_src_path, dst_path = FLAGS_dst_path]() {
         return WordCount(http_client, "/wordcount", src_path, dst_path);
       },
       false},
  };

  auto latch_count = static_cast<std::ptrdiff_t>(callings.size());
  ::trpc::FiberLatch callings_latch{latch_count};

  for (auto& c : callings) {
    ::trpc::StartFiberDetached([&callings_latch, &c]() {
      c.ok = c.calling_executor();
      callings_latch.CountDown();
    });
  }

  callings_latch.Wait();

  for (const auto& c : callings) {
    final_ok &= c.ok;
    std::cout << "name: " << c.calling_name << ", ok: " << c.ok << std::endl;
  }

  std::cout << "final result of http calling: " << final_ok << std::endl;
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
  std::cout << "FLAGS_src_path: " << FLAGS_src_path << std::endl;
  std::cout << "FLAGS_dst_path: " << FLAGS_dst_path << std::endl;
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
