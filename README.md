# myzcopy

**Zero-copy file/RAID toolkit and io_uring echo-benchmark pair for Linux, written from scratch in C.**

### Why it exists

A personal systems-programming experiment in doing file plumbing with the kernel
instead of userspace buffers: one multi-call binary (`basic.c`) that reimplements
`cat`, `tee`, `tar`, `md5sum` and software RAID 0/1/2/3/5/6/10 (plus the `un*`
inverse operations) on top of `splice`, `copy_file_range` and `MSG_ZEROCOPY`, with
a userspace fallback ladder when a primitive is unavailable; and a pair of
`io_uring` (`IORING_SETUP_SQPOLL`) echo server/client micro-benchmarks that report
accept/send/close and socket/connect/recv/close rates once per second.

### What's here

| File | What it is |
|---|---|
| `basic.c` | ~1710-line multi-call binary. Dispatch is by `argv[0]` substring: symlink it to `cat`, `tee`, `tar`, `md5sum`, `raid5`, `unraid5`, `raid10`, … Behaviour is tuned by env vars (`ZCOPY`, `RAID`, `BLOCK`, `FORCE`, `CHECK`, `UNDO`, `VERBOSE`). |
| `http.c` | Fork-per-connection HTTP/1.0 chunked server that `execl`s `/bin/sh -c` on the URL path. Toy / deliberately unsafe. |
| `hellosvr.c` | `io_uring` SQPOLL multishot-accept echo server, registered sparse files + fixed buffers, `MSG_ZEROCOPY` sends; per-second counters. |
| `hellocli.c` | Matching `io_uring` SQPOLL client; linked SQE chains socket→connect→readv→close; per-second counters. |
| `filesvr.py` | **Non-functional**: a generated `ctypes` signature stub for `liburing` — every one of ~150 functions is `pass`. Kept only as a reference of the FFI surface. |
| `Makefile` | 9 lines: `gcc -Ofast -march=native -s $< -luring -o $@`, plus `ln -sf basic <cmd>` for the multi-call symlinks. |

### How to run

```
make            # builds basic, http, hellosvr, hellocli + creates the cat/tee/tar/raid*/md5sum symlinks
./md5sum Makefile
./cat /etc/hostname
ZCOPY=0 ./cat Makefile          # force the userspace-copy fallback path
./hellosvr & ./hellocli         # io_uring benchmark pair, both on port 8080
```

Needs Linux with a modern kernel (io_uring, `splice`, `copy_file_range`,
`MSG_ZEROCOPY`) and `liburing` development headers. No non-Linux portability, no tests.

### Status

**Unmaintained (2025 snapshot) — kept as an archive.** This is *not* deprecated in the
technology sense: `io_uring`, `splice` and `copy_file_range` are all current 2026 Linux
interfaces, and there is no TensorFlow-1/fastai/Vue-style rot. What it *is* is an
unfinished personal experiment — last touched 2025-12, no README, no LICENSE, no tests,
no CI, `filesvr.py` is a stub, and `http.c` is unsafe by construction. Read it as a
snapshot of low-level I/O work, not as a maintained tool.

### License

No LICENSE file was ever added. The MD5 block in `basic.c` is an explicitly
"public-domain style" compact snippet; everything else appears to be the author's own
code. A license must be chosen before publishing.
