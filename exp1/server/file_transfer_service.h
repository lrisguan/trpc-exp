#pragma once

#include <string>
#include "trpc/server/stream_rpc_method_handler.h"
#include "proto/file_transfer.trpc.pb.h"

// 继承自 tRPC 框架根据 proto 文件自动生成的服务基类
class FileTransferServiceImpl : public trpc::exp::File::FileTransfer {
 public:
  // 重写基类中的 `UploadFile` 虚函数，以实现我们自己的业务逻辑
  ::trpc::Status UploadFile(
      const ::trpc::ServerContextPtr& context,
      const ::trpc::stream::StreamReader<::trpc::exp::File::FileChunk>& reader,
      ::trpc::exp::File::UploadResponse* response) override;
};
