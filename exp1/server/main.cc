#include <iostream>
#include "fmt/format.h"
#include "trpc/common/trpc_app.h"
#include "trpc/server/trpc_server.h"
#include "file_transfer_service.h"
#include "trpc/common/config/server_conf.h"

class FileTransferServer : public ::trpc::TrpcApp {
  public:
    int Initialize() override {
      const auto& config = ::trpc::TrpcConfig::GetInstance()->GetServerConfig();
      printf("after create config\n");
      // Set the service name, which must be the same as the value of the `/server/service/name` configuration item
      // in the configuration file, otherwise the framework cannot receive requests normally.
      std::string service_name = fmt::format("{}.{}.{}.{}", "trpc", config.app, config.server, "FileTransfer");
      printf("after create service_name\n");
      TRPC_FMT_INFO("service name:{}", service_name);
      printf("after printf info\n");

      RegisterService(service_name, std::make_shared<FileTransferServiceImpl>());
      printf("after regiter service\n");
      return 0;
    }

    void Destroy() override {}
};

int main(int argc, char** argv) {
  FileTransferServer app;
  app.Main(argc, argv);
  app.Wait();
  return 0;
}
