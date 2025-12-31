# exp1
## Compile:
```bash
cd exp1
./cmake.sh
```
After this, Makefile will be generated to **build** directory.
```bash
cd build
make -j
# or you can just run: make -C build -j
# or run: cmake --build build -j
```
Then you'd better back to this exp1's root dir.

## Run
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