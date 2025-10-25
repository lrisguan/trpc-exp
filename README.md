# tRPC-app
This is a app for transfering files based on 
[tRPC-cpp](https://github.com/trpc-group/trpc-cpp)

## 📌 Introduction
This is a FileTransfer service implemention based on tRPC-cpp.

## Quick start
- Compile **tRPC-cpp**:
> [!Tip]
> Recommended to use **gcc** whose version less than 15.1! If you use 15.1 you need
> to do a lot of works. I suggest you using **gcc** 9 or lower, but need to higher 
> than 7. May 13.2 works. But I have not give it a try, however my classmate seemed
> to use it to compile successfully.
If you want to use **gcc-9**. Please run following commands.
```bash
sudo apt install gcc-9 g++-9
```
```bash
git clone https://github.com/trpc-group/trpc-cpp.git
cd trpc-cpp
git checkout v1.2.0
cmake -B build -S . -DCMAKE_C_COMPILER=/usr/bin/gcc-9 -DCMAKE_CXX_COMPILER=/usr/bin/g++-9
cmake --build build -j$(nproc)
```
Then you'd better install them to system PATH. Just run following command.
```bash
sudo cmake --install build
```
After doing above your **trpc-cpp** will installed in: **/usr/local/trpc-cpp/trpc**

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
Then you'd better back to the repo' root dir.
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
