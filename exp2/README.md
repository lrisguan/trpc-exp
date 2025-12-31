# exp2 (Consistent Hash Micro Service)
This is a Micro Service consists of **gateway**, **servers**, and **client**. Using
cosistent hash ring to redirect http request.

## Architecture
![](../assets/exp2-arch.png)

## Compile
```bash
cd exp2
./cmake.sh
./make.sh
```
## Run
<br>

1.Start the servers.
```bash
# run server
# you can run it like this, in it, port stands for thr port you'd like to run on
./server.sh `port_1`
./server.sh `port_2`
...
./server.sh `port_n`
```
2.Then you can run the gateway.
```bash
# the port here are the port you choose in the server part.
./gateway.sh `port_1` `port_2` ... `port_n`
```
3.run the client
```bash
# download request
./download_client.sh
# upload request
./upload_client.sh
```