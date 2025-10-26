# tRPC-App

## 📌 Introduction
This is a FileTransfer service implemention based on [tRPC-Cpp](https://github.com/trpc-group/trpc-cpp).

## 🚀 Quick start
- Compile **tRPC-Cpp**:
> [!Tip]
> Recommended to use **gcc** whose version lower than **15.1** or you need
> to do a lot of works to adjust to the compilation standards. To avoid troubles
> I suggest you using **gcc-9** or lower, but need to higher than **7**. May 
> **13.2** and lower works. I had not give it a try, however my classmate seemed
> to compile successfully by **13.2**. Also you should have **cmake** version higher
> than **3.16**.

> [!Note]
> Why needs specific version of compiler? Cause the higher compiler tightens the 
> compilation standards. Take an example: When you use lower gcc, you can use data 
> type like **int64_t** without explicitly including standard file: <b>\<cstdint\></b> 
> and may you get just a <span style="color: #ae069bff">warning</span>. However, 
> in **gcc-15** you will get <span style="color: #ea1f1fff">error</span> instead 
> and that will result the compilation process ending.

If you want to use **gcc-9**. Please run following commands.
- **Debian/Ubuntu**
```bash
sudo apt install gcc-9 g++-9
```
- **CentOS7**
```bash
sudo yum install centos-release-scl
sudo yum install devtoolset-9-gcc devtoolset-9-g++
scl enable devtoolset-9 bash
```
```bash
git clone https://github.com/trpc-group/trpc-cpp.git
cd trpc-cpp
git checkout v1.2.0
cmake -B build -S . -DCMAKE_C_COMPILER=/usr/bin/gcc-9 -DCMAKE_CXX_COMPILER=/usr/bin/g++-9
cmake --build build -j$(nproc)
```
You'd better install them to system PATH. Just run following command.
```bash
sudo cmake --install build
```
After doing above your **trpc-cpp** will installed in: **/usr/local/trpc-cpp/trpc**.

- Compile **FileTransfer**:
You need to find a place to load this repo.
```bash
git clone https://github.com/gzqccnu/trpc-app.git
cd trpc-app
./cmake.sh
```
After this, Makefile will be generated to **build** directory.
```bash
cd build
make -j
```
Then you'd better back to this repo's root dir.
```bash
cd ..
# open one terminal to run the server
# you can use script to launch server
# ./server.sh
# or
./build/file_transfer_server --config=./server/trpc_server_config.yaml
# open another terminal to run the client
# you can use script to launch client
# ./client.sh
# or
./build/file_transfer_client --client_config=./client/trpc_client_config.yaml
```
