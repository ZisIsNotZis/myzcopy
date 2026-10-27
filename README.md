**English** | [简体中文](README.zh-CN.md)

<p align="center"><img src="assets/logo.svg" alt="myzcopy" width="200"></p>

# myzcopy

*Zero-copy file plumbing and software RAID in one C binary, plus an `io_uring` benchmark pair.*

**It moves files, archives, checksums and RAID stripes between file descriptors with `copy_file_range`, `splice` and double-`splice` so the page cache does the work instead of userspace buffers, and it measures what an `io_uring` echo loop can sustain on loopback.**

The whole toolkit is one ~1,700-line multi-call binary: you symlink it to the name of the tool you want and it dispatches on `argv[0]`. When a primitive is missing — an old kernel, a filesystem that refuses `copy_file_range`, a pipe that cannot be spliced — every command degrades through a fixed ladder down to plain `read`/`write` instead of failing.

## Quick start

Linux, a modern kernel, `gcc` and the `liburing` headers (`liburing-dev` on Debian/Ubuntu):

```sh
git clone https://github.com/ZisIsNotZis/myzcopy.git
cd myzcopy
make                      # builds basic, http, hellosvr, hellocli and the symlinks
```

```sh
# split a file into chunks and put it back together
dd if=/dev/urandom of=disk.img bs=1M count=4
./cat disk.img part0 part1 part2
./uncat disk.img part0 part1 part2

# fan one file out to several copies
./tee disk.img copy0 copy1

# tar, with no external tar
./tar archive.tar README.md basic.c
./untar archive.tar

# checksums, then verify against the file you wrote
./md5sum sums.md5 README.md basic.c
CHECK=1 ./md5sum sums.md5 README.md basic.c

# software RAID 5 across three parts, then rebuild
./raid5 disk.img d0 d1 d2
./unraid5 disk.img d0 d1 d2

# force the userspace-copy fallback on any of the above
ZCOPY=0 ./cat disk.img p0 p1
```

Two processes, two terminals, one benchmark:

```sh
./hellosvr     # io_uring SQPOLL echo server on 127.0.0.1:8080, prints counters once a second
./hellocli     # 2048 linked socket→connect→readv→close chains, prints counters once a second
```

`./http` is a separate, deliberately unsafe toy: an HTTP/1.0 chunked server that `fork`s per connection and runs the URL-decoded path through `/bin/sh -c`.

## What it can do now

- **Multi-call dispatch** — `basic.c` picks its command from its own name: `(un)cat`, `(un)tee`, `(un)tar`, `(un)md5sum`, `(un)raid0/1/2/3/5/6/10`. The `un*` names run the inverse operation.
- **A zero-copy ladder** — `copy_file_range` → `splice` → double-`splice` through a pipe (with `lseek` restore for seekable input) → `read`/`write`. `ZCOPY=3|2|1|0` pins the starting rung; the default is `2` (`splice`) and each failure steps down one rung instead of returning an error.
- **Chunked split / join** — `cat` cuts a file into N byte-balanced chunks, `uncat` concatenates them back; existing chunks can be skipped, overwritten (`FORCE=1`) or checked (`CHECK=1`, non-zero exit on a missing part).
- **File fan-out** — `tee` finds the first existing regular file in its argument list and copies it to all the others, in zero-copy mode; it never reads stdin.
- **Self-contained tar** — a minimal `ustar`-style writer/extractor: 512-byte headers, directory recursion, symlinks, parent-directory creation, permission and mtime restore.
- **Self-contained MD5** — `md5sum` writes `<digest> <path>` lines, `CHECK=1` re-reads that file, compares line by line and exits non-zero on any mismatch or missing entry.
- **Software RAID** — levels 0, 1, 3, 5, 6 and 10: striping, mirroring, and parity / double-parity / triple-parity layouts over GF(2⁸). `unraid*` reassembles the image and, with missing parts, rebuilds them from the parity equations — up to the parity width, using a small Gaussian solver (`gf_solve`) when more than one part has to be solved for. `BLOCK` sets the stripe unit; `FORCE`/`CHECK` control overwriting and in-place verification of stale parity.
- **io_uring benchmark pair** — SQPOLL rings, registered sparse file tables, multishot `accept_direct`, `IOSQE_FIXED_FILE`/`IO_LINK`/`IO_HARDLINK` chains, `MSG_ZEROCOPY` sends; per-second accept/send/close and socket/connect/recv/close counters.
- **Tunables by environment** — `ZCOPY`, `RAID`, `BLOCK`, `FORCE`, `CHECK`, `UNDO`, `VERBOSE` (0 silent … 2 verbose) — no config files, no flags.

## Architecture

There is no library and no shared header: each `.c` file is a standalone program, and the only coupling is that `basic.c` is compiled once and hard-linked by name to every command.

```text
basic.c      multi-call binary: dispatch by argv[0]
             ├── zcopy ladder      copy_file_range / splice / double-splice / read+write
             ├── md5               self-contained MD5
             ├── tar               ustar create + extract
             └── raid + gf(2^8)    striping, mirror, parity, rebuild solver
http.c       fork-per-connection chunked HTTP toy, splice(pipe → socket) + MSG_ZEROCOPY
hellosvr.c   io_uring SQPOLL echo server, 127.0.0.1:8080
hellocli.c   io_uring SQPOLL client, 2048 concurrent chains, same port
filesvr.py   non-functional ctypes signature stub for liburing (reference only)
Makefile     gcc -Ofast -march=native -s <x>.c -luring -o <x>, plus the symlinks
docs/        project status / provenance note
```

`basic.c` parses its environment once in `main`, then hands off to one `do_*` function; data never passes through it — every command funnels into `zcopy`/`zcopy_len`, which is the only place that knows how bytes actually move. The two `io_uring` programs share no code with it: they are an independent measurement of the same idea, that the kernel should move the bytes.

## How to develop

```sh
make            # build everything (also the symlinks)
make clean      # remove the binaries; the symlinks stay
```

There is no test suite, no linter config and no CI. The build is one implicit `gcc` rule per file, so iterating means editing a `.c` file and running `make` again. Because `basic.c` is built with `-Ofast -march=native`, binaries are host-specific by design — rebuild rather than copy them between machines. `VERBOSE=2` (the default) prints the chosen zero-copy rung and per-file actions to stderr, which is how the ladder is inspected.

## How to contribute

See [CONTRIBUTING.md](CONTRIBUTING.md). This is an archived personal experiment: bug reports and small, self-contained fixes are welcome; large redesigns are unlikely to be merged.

## Roadmap

**Implemented**

- Multi-call `cat`/`uncat`, `tee`, `tar`/`untar`, `md5sum`, `raid*`/`unraid*` on the zero-copy ladder.
- RAID 0, 1, 3, 5, 6 and 10 with a GF(2⁸) rebuild solver; level 2 is recognized by the dispatcher but not implemented (`unsupported raid mode`).
- `io_uring` SQPOLL echo server and client with per-second counters.
- Fork-per-connection chunked HTTP toy with `splice` + `MSG_ZEROCOPY`.

**Not planned — this repository is an archive**

`filesvr.py` is a generated `ctypes` stub in which every one of its ~150 `liburing` bindings is `pass`; making it real would mean writing a `liburing` FFI surface from scratch. `http.c` is unsafe by construction (command injection through the URL path) and is kept as a demo, not fixed. What a future version would plausibly need: a test suite around the zero-copy ladder and the RAID round-trips, a build that does not require `-march=native`, and a sandboxed replacement for `http.c`. None of that is in progress; the repository is a 2025 snapshot kept for reference.

## License

GNU AGPL-3.0-only — see [LICENSE](LICENSE). The MD5 block in `basic.c` is an explicitly public-domain-style compact snippet; everything else appears to be the author's own code.
