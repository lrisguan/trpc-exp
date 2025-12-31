# tRPC-EXP

## 📌 Introduction
There are experiments implemention of `Cloud Computing` based on RPC framework of 
[tRPC-Cpp](https://github.com/trpc-group/trpc-cpp).

## 🚀 Quick start
- Compile **tRPC-Cpp** as a standalone lib:
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

> [!Warning]
> When compiling this project, I using gcc-9 and g++-9. 
> That means may in a higher version of gcc and g++, you need to add **\<cstdint\>** by your self.

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
# create a soft link for avoiding subsequent cmake errors.
ln -s /usr/local/trpc-cpp/trpc/cmake/config/trpc_config.cmake /usr/local/trpc-cpp/trpc/cmake/config/trpcConfig.cmake
```
After doing above your **trpc-cpp** will installed in: **/usr/local/trpc-cpp/trpc**.

> [!Note]
> Below are all for experiments.

The first step, you need to clone this repo.
```bash
git clone https://github.com/lrisguan/trpc-app.git
cd trpc-app
```
- **exp1:**
    Please go to [exp1](./exp1/README.md) for details.
- **exp2:**
    Please go to [exp2](./exp2/README.md) for details.
- **exp3**
    Please go to [exp3](./exp3/README.md) for details.