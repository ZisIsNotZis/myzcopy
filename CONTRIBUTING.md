# Contributing

`myzcopy` is an archived personal systems-programming experiment. It is not
under active development, but small, well-scoped fixes are welcome.

## Scope

In scope: correctness fixes in `basic.c` (zero-copy ladder, tar, MD5, RAID
rebuild), portability fixes that keep the existing design, and clear bug
reports with a reproduction.

Out of scope: rewrites, new dependencies, turning `filesvr.py` into a real
`liburing` binding layer, hardening `http.c` into a production server. Open an
issue to discuss a large change before writing it.

## Running the checks

There is no test suite yet, so "checks" means: it builds, and the affected
command still behaves. Build and exercise by hand:

```sh
make                      # must be warning-free apart from existing ones
VERBOSE=2 ./cat big.bin p0 p1 && ./uncat big.bin p0 p1
./md5sum sums.md5 Makefile && CHECK=1 ./md5sum sums.md5 Makefile
./raid5 disk.img d0 d1 d2 && rm disk.img && ./unraid5 disk.img d0 d1 d2
```

If your change touches the zero-copy path, run the same command with
`ZCOPY=3`, `ZCOPY=2`, `ZCOPY=1` and `ZCOPY=0` and confirm identical output.
If it touches RAID, compare the round-tripped image byte-for-byte
(`cmp original rebuilt`).

## Pull requests

- One concern per commit, Conventional Commit messages (`fix:`, `docs:`,
  `refactor:`).
- Say which commands you ran and what they printed; there is no CI to do it
  for you.
- Keep the multi-call design: behaviour switches on `argv[0]` and environment
  variables, not on new flags.
- Do not reformat untouched code or add CI, linters or build systems.

## Issues

Include the kernel version (`uname -r`), filesystem, the exact command, the
`VERBOSE=2` output, and the value of any `ZCOPY`/`RAID`/`BLOCK` you set. That
is usually enough to tell which rung of the ladder or which RAID branch was
taken.
