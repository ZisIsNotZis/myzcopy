[English](README.md) | **简体中文**

<p align="center"><img src="assets/logo.svg" alt="myzcopy" width="200"></p>

# myzcopy

*一个用 C 写成的零拷贝文件工具 + 软 RAID 多调用二进制，外加一对 `io_uring` 基准测试程序。*

**它靠 `copy_file_range`、`splice` 和双 `splice` 在文件描述符之间搬运文件、归档、校验和与 RAID 条带，让页缓存干活而不是用户态缓冲区；同时测量 `io_uring` 回声环在 loopback 上能跑到什么程度。**

整套工具就是一个约 1700 行的多调用二进制：把它软链成想要的名字，它按 `argv[0]` 分派。遇到内核太老、文件系统拒绝 `copy_file_range`、管道不能 splice 之类的情况，每条命令都会沿一条固定的降级阶梯一路退到最朴素的 `read`/`write`，而不是直接报错。

## 快速开始

需要 Linux、较新的内核、`gcc` 和 `liburing` 头文件（Debian/Ubuntu 上是 `liburing-dev`）：

```sh
git clone https://github.com/ZisIsNotZis/myzcopy.git
cd myzcopy
make                      # 编译 basic、http、hellosvr、hellocli，并创建软链
```

```sh
# 切块再拼回去
dd if=/dev/urandom of=disk.img bs=1M count=4
./cat disk.img part0 part1 part2
./uncat disk.img part0 part1 part2

# 把一个文件复制成多份
./tee disk.img copy0 copy1

# 不使用外部 tar 的打包/解包
./tar archive.tar README.md basic.c
./untar archive.tar

# 生成校验和，再用它校验
./md5sum sums.md5 README.md basic.c
CHECK=1 ./md5sum sums.md5 README.md basic.c

# 三块盘上的软 RAID 5，然后重组
./raid5 disk.img d0 d1 d2
./unraid5 disk.img d0 d1 d2

# 强制走用户态拷贝的兜底路径
ZCOPY=0 ./cat disk.img p0 p1
```

基准测试要两个进程、两个终端：

```sh
./hellosvr     # io_uring SQPOLL 回声服务端，127.0.0.1:8080，每秒打印一次计数
./hellocli     # 2048 条链接起来的 socket→connect→readv→close，每秒打印一次计数
```

`./http` 是另一个东西，一个故意不安全的玩具：HTTP/1.0 chunked 服务端，每个连接 fork 一次，把 URL 解码后的路径交给 `/bin/sh -c` 执行。

## 现在能做什么

- **多调用分派** —— `basic.c` 从自己的名字决定行为：`(un)cat`、`(un)tee`、`(un)tar`、`(un)md5sum`、`(un)raid0/1/2/3/5/6/10`。带 `un` 的名字执行反向操作。
- **零拷贝降级阶梯** —— `copy_file_range` → `splice` → 经管道的双 `splice`（对可寻址输入会 `lseek` 复位）→ `read`/`write`。`ZCOPY=3|2|1|0` 指定起始档位，默认 `2`（`splice`）；某一档失败就降一档，而不是返回错误。
- **按块切分 / 合并** —— `cat` 把文件切成 N 个大小均衡的块，`uncat` 再拼回去；已存在的块可以跳过、覆盖（`FORCE=1`）或校验（`CHECK=1`，缺块时以非零码退出）。
- **一写多份** —— `tee` 在参数里找到第一个存在的普通文件，然后以零拷贝方式复制到其余所有路径；它从不读 stdin。
- **自带的 tar** —— 极简的 `ustar` 写入/解包：512 字节头部、递归目录、软链接、自动建父目录、恢复权限和 mtime。
- **自带的 MD5** —— `md5sum` 写出 `<摘要> <路径>` 行；`CHECK=1` 时重新读该文件、逐行比较，任何不匹配或缺少条目都以非零码退出。
- **软 RAID** —— 0、1、3、5、6、10 级：条带、镜像，以及基于 GF(2⁸) 的单校验、双校验、三校验布局。`unraid*` 重组镜像；有部件缺失时按校验方程重建——最多可到校验宽度，需要同时求解多个未知量时用一个小的 GF(2⁸) 高斯消元（`gf_solve`）。`BLOCK` 设条带单元，`FORCE`/`CHECK` 控制覆盖与就地校验过期校验块。
- **io_uring 基准对** —— SQPOLL 环、注册的稀疏文件表、multishot `accept_direct`、`IOSQE_FIXED_FILE`/`IO_LINK`/`IO_HARDLINK` 链、`MSG_ZEROCOPY` 发送；每秒打印 accept/send/close 与 socket/connect/recv/close 计数。
- **用环境变量调参** —— `ZCOPY`、`RAID`、`BLOCK`、`FORCE`、`CHECK`、`UNDO`、`VERBOSE`（0 静默 … 2 详细），没有配置文件，也没有命令行开关。

## 架构

没有库，也没有共享头文件：每个 `.c` 都是独立程序，唯一的耦合是 `basic.c` 只编译一次，再按名字硬链成每条命令。

```text
basic.c      多调用二进制：按 argv[0] 分派
             ├── zcopy 阶梯        copy_file_range / splice / 双 splice / read+write
             ├── md5               自带的 MD5
             ├── tar               ustar 打包 + 解包
             └── raid + gf(2^8)    条带、镜像、校验、重建求解
http.c       每连接 fork 的 chunked HTTP 玩具，splice(管道 → socket) + MSG_ZEROCOPY
hellosvr.c   io_uring SQPOLL 回声服务端，127.0.0.1:8080
hellocli.c   io_uring SQPOLL 客户端，2048 条并发链，同一端口
filesvr.py   不能用的 liburing ctypes 签名桩（仅作参考）
Makefile     gcc -Ofast -march=native -s <x>.c -luring -o <x>，外加软链
docs/        项目状态 / 来源说明
```

`basic.c` 在 `main` 里读一次环境变量，然后交给某个 `do_*` 函数；数据不经过它——每条命令最终都汇到 `zcopy`/`zcopy_len`，只有那里知道字节到底怎么移动。两个 `io_uring` 程序跟它不共享任何代码：它们是对同一想法的独立测量——字节该由内核来搬。

## 如何开发

```sh
make            # 编译全部（包括软链）
make clean      # 删除二进制，软链会留下
```

没有测试套件、没有 linter 配置、没有 CI。构建就是每个文件一条隐式 `gcc` 规则，所以迭代方式就是改 `.c` 再跑一次 `make`。由于 `basic.c` 用 `-Ofast -march=native` 编译，生成的二进制本来就是针对本机的——换机器请重新编译，不要直接拷。`VERBOSE=2`（默认值）会把选中的零拷贝档位和逐文件动作打到 stderr，这就是观察降级阶梯的方式。

## 如何贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)。这是一个归档的个人实验：欢迎缺陷报告和小而独立的修复；大改重写基本不会被合并。

## 路线图

**已实现**

- 零拷贝阶梯上的 `cat`/`uncat`、`tee`、`tar`/`untar`、`md5sum`、`raid*`/`unraid*` 多调用命令。
- RAID 0、1、3、5、6、10 及 GF(2⁸) 重建求解；2 级能被分派器识别，但未实现（会打印 `unsupported raid mode`）。
- `io_uring` SQPOLL 回声服务端与客户端，loopback 上每秒计数。
- 每连接 fork 的 chunked HTTP 玩具，含 `splice` + `MSG_ZEROCOPY`。

**未计划 —— 这个仓库是归档**

`filesvr.py` 是生成的 `ctypes` 桩，约 150 个 `liburing` 绑定每个都是 `pass`；要让它真能用，等于从零写一套 `liburing` FFI。`http.c` 从一开始就不安全（URL 路径可注入命令），它作为演示保留，不打算修。将来若真要往下做，缺的大致是：围绕零拷贝阶梯和 RAID 往返的测试套件、不依赖 `-march=native` 的构建、以及一个沙箱化的 `http.c` 替代品。这些都不在进行中；本仓库是 2025 年的快照，留作参考。

## 许可证

GNU AGPL-3.0-only —— 见 [LICENSE](LICENSE)。`basic.c` 里的 MD5 部分是明确标注的公有领域风格紧凑代码片段；其余看起来都是作者自己的代码。
