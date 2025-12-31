# exp3 (Map-Reduce of Word Count)

## Compile
```bash
cd exp3
./cmake.sh
./make.sh
```

## Run
1.Lauch servers
```bash
/server.sh `port_1`
./server.sh `port_2`
...
./server.sh `port_n`
```
2.start gateway
```bash
./gateway.sh `port_1` `port_2` ... `port_n`
```
3.upload the dataset
```bash
./dataset_client.sh upload dataset vocab.txt `n`
```
4.wordcount compute distributly
```bash
./dataset_client.sh wc datasset `n`
```
5.wordcount singly
```bash
./wordcount_client.sh
```

## Result
```bash
data 224
file 224
system 224
memory 192
kernel 160
computer 159
directory 128
processor 128
thread 128
process 112
architecture 96
code 96
network 96
programming 96
structure 96
algorithm 80
cache 80
device 80
driver 80
protocol 80
security 80
task 80
test 80
api 64
bug 64
build 64
compiler 64
debugger 64
deploy 64
error 64
execution 64
fix 64
instruction 64
internet 64
interpreter 64
register 64
client 48
database 48
gui 48
library 48
optimization 48
performance 48
server 48
version 48
control 47
controlcomputer 1
```