#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "trpc/client/trpc_client.h"
#include "trpc/client/make_client_context.h"
#include "gflags/gflags.h"
#include "proto/file_transfer.trpc.pb.h"
#include "trpc/common/runtime_manager.h"

DEFINE_string(client_config, "trpc_client_config.yaml", "framework client_config file, --client_config=trpc_cpp.yaml");
DEFINE_string(service_name, "trpc.exp.File.FileTransfer", "callee service name");

int DoRpcCall(const std::shared_ptr<::trpc::exp::File::FileTransferServiceProxy>& proxy) {
  ::trpc::ClientContextPtr client_ctx = ::trpc::MakeClientContext(proxy);
  std::cout << "In DoRpcCall" << std::endl;
  client_ctx->SetTimeout(10000); // 设置10秒超时

  // ----------------------------------------------------------------
  // 核心逻辑开始
  // ----------------------------------------------------------------

  // 1. 发起 RPC 调用，获取流写入器
  std::cout << "Step 1: Initiating RPC call to get stream writer" << std::endl;
  trpc::exp::File::UploadResponse response;
  trpc::stream::StreamWriter<::trpc::exp::File::FileChunk> stream_writer = proxy->UploadFile(client_ctx, &response);
  if (stream_writer.GetStatus().OK() == false) {
    std::cerr << "创建流写入器失败, ret=" << client_ctx->GetStatus().GetFrameworkRetCode()
              << ", msg=" << client_ctx->GetStatus().ErrorMessage() << std::endl;
    return -1;
  }
  
  // 2. 准备要发送的文件
  std::cout << "Step 2: Preparing file to send" << std::endl;
  const std::string filename = "sample.txt";
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "错误: 无法打开文件 " << filename << std::endl;
    return -1;
  }

  std::cout << "客户端开始发送文件: " << filename << std::endl;

  // 3. 循环读取文件并写入流中
  std::cout << "Step 3: Reading file and writing to stream" << std::endl;
  const size_t buffer_size = 4096; // 每次读取 4KB
  std::vector<char> buffer(buffer_size);

  while (!file.eof()) {
    file.read(buffer.data(), buffer_size);
    std::streamsize bytes_read = file.gcount(); // 获取实际读取的字节数

    if (bytes_read > 0) {
      trpc::exp::File::FileChunk chunk;
      chunk.set_data(buffer.data(), bytes_read);

      // 通过流写入器发送文件块
      // TO-DO: 学生需要理解这行代码的作用
      if (stream_writer.Write(chunk).OK() == false) {
        std::cerr << "写入流失败" << std::endl;
        break; // 写入失败则跳出循环
      }
    }
  }

  file.close();

  // 4. 发送结束信号
  // TO-DO: 学生需要理解为什么这行代码是必须的
  // 这是告诉服务端：“我的数据已经全部发完了”
  if (stream_writer.WriteDone().OK() == false) {
      std::cerr << "发送 WritesDone 信号失败" << std::endl;
      return -1;
  }
  std::cout << "客户端文件发送完毕" << std::endl;

  // 5. 等待服务端的最终响应
  // TO-DO: 学生需要理解 Finish 的作用
  // 程序会在这里阻塞，直到服务端处理完所有数据并返回一个 UploadResponse
  trpc::Status status = stream_writer.Finish();

  // ----------------------------------------------------------------
  // 核心逻辑结束
  // ----------------------------------------------------------------

  if (status.OK()) {
    std::cout << "客户端收到响应: success=" << std::boolalpha << response.success()
              << ", message=\"" << response.message() << "\"" << std::endl;
  } else {
    std::cerr << "RPC 调用失败: " << status.ErrorMessage() << std::endl;
  }

  return 0;
}

int Run() {
  auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::exp::File::FileTransferServiceProxy>(FLAGS_service_name);

  if (!proxy) {
    std::cerr << "错误：无法获取服务代理！请检查配置文件中的服务名 [" 
              << FLAGS_service_name << "] 是否与服务端配置匹配，或检查配置文件加载是否成功。" << std::endl;
    return -1; // 如果 proxy 无效，返回错误码，避免空指针解引用
  }
  std::cout << "Success in Run()" << "FLAGS_service_name: " << FLAGS_service_name << std::endl;
  return DoRpcCall(proxy);
}

void ParseClientConfig(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::CommandLineFlagInfo info;
  if (GetCommandLineFlagInfo("client_config", &info) && info.is_default) {
    std::cerr << "start client with client_config, for example: " << argv[0]
              << " --client_config=/client/client_config/filepath" << std::endl;
    exit(-1);
  }

  std::cout << "FLAGS_service_name:" << FLAGS_service_name << std::endl;
  std::cout << "FLAGS_client_config:" << FLAGS_client_config << std::endl;

  int ret = ::trpc::TrpcConfig::GetInstance()->Init(FLAGS_client_config);
  if (ret != 0) {
    std::cerr << "load client_config failed." << std::endl;
    exit(-1);
  }
}

int main(int argc, char** argv) {
  ParseClientConfig(argc, argv);
  std::cout << "ClientConfig loaded OK" << std::endl;
  // If the business code is running in trpc pure client mode,
  // the business code needs to be running in the `RunInTrpcRuntime` function
  return ::trpc::RunInTrpcRuntime([]() { return Run(); });

  return 0;
}


