#include "file_transfer_service.h"
#include <fstream>
#include <iostream>

::trpc::Status FileTransferServiceImpl::UploadFile(
      const ::trpc::ServerContextPtr& context,
      const ::trpc::stream::StreamReader<::trpc::exp::File::FileChunk>& reader,
      ::trpc::exp::File::UploadResponse* response) {
  std::cout << "服务端收到一个文件上传请求" << std::endl;

  // 1. 准备接收文件
  const std::string output_filename = "received_file.txt";
  std::ofstream output_file(output_filename, std::ios::binary);
  

  if (!output_file.is_open()) {
    std::cerr << "服务端错误: 无法创建文件 " << output_filename << std::endl;
    response->set_success(false);
    response->set_message("Server error: cannot create file.");
    return trpc::Status(trpc::TrpcRetCode::TRPC_SERVER_SYSTEM_ERR, "Cannot create output file");
  }

  trpc::exp::File::FileChunk chunk;
  size_t total_bytes_received = 0;

  // 2. TO-DO: 循环读取客户端发来的数据流
  // 当客户端调用 WritesDone() 后，Read() 会返回 false，循环结束
  trpc::Status status;
  while ((status = reader.Read(&chunk)).OK()) {
    // 3. TO-DO: 将接收到的数据块写入文件
    output_file.write(chunk.data().c_str(), chunk.data().length());
    total_bytes_received += chunk.data().length();
  }

  // 4. 关闭文件并准备响应
  output_file.close();
  std::cout << "服务端文件接收完毕，总共接收 " << total_bytes_received << " 字节" << std::endl;

  // 5. TO-DO: 设置成功的响应
  response->set_success(true);
  response->set_message("文件接收完毕并成功保存");

  // 6. 打印文件接收到的内容
  std::fstream bf("received_file.txt", std::ios::in | std::ios::binary);
  if (!bf) {
      std::cerr << "this binary file can't open" << std::endl;
      exit(1);
  }

  // 移动到文件末尾来获取大小
  bf.seekg(0, std::ios::end);
  std::streampos size = bf.tellg();
  bf.seekg(0, std::ios::beg);

  // 读取所有字节
  std::vector<char> buffer(size);
  bf.read(buffer.data(), size);

  // 打印原始字节（可能包含不可见字符）
  std::cout << "File size: " << size << " bytes\n";
  std::cout << "Raw content:\n";
  for (unsigned char c : buffer) {
      std::cout << c;  // 如果是文本文件，可以直接输出
  }

  bf.close();

  return ::trpc::kSuccStatus;
}
