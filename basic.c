#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#define CHUNK (64 * 1024)
#define TAR_BLOCK 512
/* --- small logging helpers --- */
int ZCOPY, RAID, BLOCK, FORCE, CHECK, UNDO, VERBOSE;
void vinfo(const char *fmt, ...) {
  if (VERBOSE < 2)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}
void vwarn(const char *fmt, ...) {
  if (VERBOSE < 1)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}
void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}
int env(const char *k, int d) {
  char *e = getenv(k);
  return e ? atoi(e) : d;
}
int write_all(int fd, const void *buf, size_t n) {
  const char *p = buf;
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    p += w;
    n -= w;
  }
  return 0;
}
void xor_blocks(unsigned char *dst, const unsigned char *src, size_t n) {
  for (size_t i = 0; i < n; i++)
    dst[i] ^= src[i];
}
/* --- small MD5 implementation (public-domain style) --- */
/* source: compact MD5 available in many public domain snippets */
typedef struct {
  uint32_t h[4];
  uint64_t len;
  unsigned char buf[64];
} MD5_CTX;
void md5_init(MD5_CTX *c) {
  c->h[0] = 0x67452301;
  c->h[1] = 0xEFCDAB89;
  c->h[2] = 0x98BADCFE;
  c->h[3] = 0x10325476;
  c->len = 0;
}
inline uint32_t R(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
void md5_transform(MD5_CTX *c, const unsigned char *b) {
  uint32_t a = c->h[0], d = c->h[1], c0 = c->h[2], b0 = c->h[3], M[16];
  for (int i = 0; i < 16; i++)
    M[i] = (uint32_t)b[i * 4] | ((uint32_t)b[i * 4 + 1] << 8) | ((uint32_t)b[i * 4 + 2] << 16) | ((uint32_t)b[i * 4 + 3] << 24);
  uint32_t A = a, B = d, C = c0, D = b0;
  const uint32_t K[] = {0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  const int S[] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  for (int i = 0; i < 64; i++) {
    uint32_t F, g;
    if (i < 16) {
      F = (B & C) | ((~B) & D);
      g = i;
    } else if (i < 32) {
      F = (D & B) | ((~D) & C);
      g = (5 * i + 1) & 15;
    } else if (i < 48) {
      F = B ^ C ^ D;
      g = (3 * i + 5) & 15;
    } else {
      F = C ^ (B | (~D));
      g = (7 * i) & 15;
    }
    uint32_t tmp = D;
    D = C;
    C = B;
    B = B + R(A + F + K[i] + M[g], S[i]);
    A = tmp;
  }
  c->h[0] += A;
  c->h[1] += B;
  c->h[2] += C;
  c->h[3] += D;
}
void md5_update(MD5_CTX *c, const unsigned char *data, size_t len) {
  size_t idx = c->len & 63;
  c->len += len;
  if (idx) {
    size_t need = 64 - idx;
    if (len < need) {
      memcpy(c->buf + idx, data, len);
      return;
    }
    memcpy(c->buf + idx, data, need);
    md5_transform(c, c->buf);
    data += need;
    len -= need;
  }
  while (len >= 64) {
    md5_transform(c, data);
    data += 64;
    len -= 64;
  }
  if (len)
    memcpy(c->buf, data, len);
}
void md5_final(MD5_CTX *c, unsigned char out[16]) {
  uint64_t bits = c->len << 3;
  size_t idx = c->len & 63;
  c->buf[idx++] = 0x80;
  if (idx > 56) {
    while (idx < 64)
      c->buf[idx++] = 0;
    md5_transform(c, c->buf);
    idx = 0;
  }
  while (idx < 56)
    c->buf[idx++] = 0;
  for (int i = 0; i < 8; i++)
    c->buf[56 + i] = (unsigned char)(bits >> (8 * i));
  md5_transform(c, c->buf);
  for (int i = 0; i < 4; i++) {
    out[i * 4 + 0] = (unsigned char)(c->h[i] & 0xff);
    out[i * 4 + 1] = (unsigned char)((c->h[i] >> 8) & 0xff);
    out[i * 4 + 2] = (unsigned char)((c->h[i] >> 16) & 0xff);
    out[i * 4 + 3] = (unsigned char)((c->h[i] >> 24) & 0xff);
  }
}
/* --- GF helpers for RAID --- */
unsigned char gf_exp[512], gf_log[256];
void gf_init() {
  unsigned char v = 1;
  for (int i = 0; i < 255; i++) {
    gf_exp[i] = v;
    gf_log[v] = i;
    unsigned char hi = v & 0x80;
    v <<= 1;
    if (hi)
      v ^= 0x1d;
  }
  for (int i = 255; i < 512; i++)
    gf_exp[i] = gf_exp[i - 255];
  gf_exp[255] = 1;
  gf_log[0] = 0;
}
unsigned char gf_mul(unsigned char a, unsigned char b) {
  if (!a || !b)
    return 0;
  return gf_exp[(gf_log[a] + gf_log[b]) % 255];
}
unsigned char gf_div(unsigned char a, unsigned char b) {
  if (a == 0)
    return 0;
  if (b == 0)
    return 0;
  return gf_exp[(gf_log[a] + 255 - gf_log[b]) % 255];
}
/* --- zero-copy helpers (as before) --- */
ssize_t try_cfr(int infd, loff_t *off_in, int outfd, size_t want) {
  ssize_t r;
  do {
    r = copy_file_range(infd, off_in, outfd, NULL, want, 0);
  } while (r < 0 && errno == EINTR);
  return r;
}
ssize_t try_splice_direct_len(int infd, int outfd, size_t want) {
  ssize_t tot = 0;
  for (;;) {
    size_t wn = (want == (size_t)-1) ? CHUNK : (want > CHUNK ? CHUNK : want);
    ssize_t n = splice(infd, NULL, outfd, NULL, wn, SPLICE_F_MOVE | SPLICE_F_MORE);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      break;
    tot += n;
    if (want != (size_t)-1) {
      want -= n;
      if (want == 0)
        break;
    }
  }
  return tot;
}
ssize_t try_double_splice_restore_len(int infd, int outfd, size_t want) {
  int p[2];
  if (pipe(p) < 0)
    return -1;
  char *dr = malloc(CHUNK);
  if (!dr) {
    close(p[0]);
    close(p[1]);
    return -1;
  }
  off_t saved = lseek(infd, 0, SEEK_CUR);
  bool can_seek = (saved != (off_t)-1);
  ssize_t total = 0;
  for (;;) {
    size_t wn = (want == (size_t)-1) ? CHUNK : (want > CHUNK ? CHUNK : want);
    ssize_t n1 = splice(infd, NULL, p[1], NULL, wn, SPLICE_F_MOVE | SPLICE_F_MORE);
    if (n1 < 0) {
      if (errno == EINTR)
        continue;
      free(dr);
      close(p[0]);
      close(p[1]);
      return -1;
    }
    if (n1 == 0)
      break;
    ssize_t wrote = 0;
    while (wrote < n1) {
      ssize_t n2 = splice(p[0], NULL, outfd, NULL, n1 - wrote, SPLICE_F_MOVE | SPLICE_F_MORE);
      if (n2 < 0) {
        if (errno == EINTR)
          continue;
        ssize_t left = n1 - wrote;
        while (left > 0) {
          ssize_t r = read(p[0], dr, left > CHUNK ? CHUNK : left);
          if (r < 0) {
            if (errno == EINTR)
              continue;
            break;
          }
          if (r == 0)
            break;
          left -= r;
        }
        if (can_seek) {
          if (lseek(infd, saved, SEEK_SET) < 0)
            vwarn("restore failed: %s\n", strerror(errno));
        } else
          vwarn("input not seekable; cannot restore after partial "
                "double-splice\n");
        free(dr);
        close(p[0]);
        close(p[1]);
        return -1;
      }
      wrote += n2;
      total += n2;
    }
    if (want != (size_t)-1) {
      want -= wrote;
      if (want == 0)
        break;
    }
  }
  free(dr);
  close(p[0]);
  close(p[1]);
  return total;
}
ssize_t try_user_copy_len(int infd, int outfd, char *buf, size_t want) {
  ssize_t tot = 0;
  while (want > 0) {
    size_t wn = want > CHUNK ? CHUNK : want;
    ssize_t r = read(infd, buf, wn);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (r == 0)
      break;
    if (write_all(outfd, buf, r) < 0)
      return -1;
    tot += r;
    want -= r;
  }
  return tot;
}
ssize_t try_user_copy_dup(int infd, int *outs, int nouts, char *buf) {
  ssize_t tot = 0;
  for (;;) {
    ssize_t r = read(infd, buf, CHUNK);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (r == 0)
      break;
    for (int i = 0; i < nouts; i++)
      if (write_all(outs[i], buf, r) < 0)
        vwarn("write to fd %d failed\n", outs[i]);
    tot += r;
  }
  return tot;
}
/* zcopy_len */
ssize_t zcopy_len(int infd, int nouts, size_t len, int *outs) {
  char *buf = malloc(CHUNK);
  if (!buf)
    die("malloc");
  off_t saved = lseek(infd, 0, SEEK_CUR);
  bool seekable = (saved != (off_t)-1);
  if (nouts > 1 && !seekable) {
    vinfo("zcopy: non-seekable input + multiple outputs -> userspace "
          "\nduplication");
    ssize_t r = try_user_copy_dup(infd, outs, nouts, buf);
    free(buf);
    return r;
  }
  if (nouts > 1 && seekable) {
    vinfo("zcopy: duplicating using copy_file_range\n");
    struct stat st;
    if (fstat(infd, &st) < 0) {
      free(buf);
      die("fstat");
    }
    off_t file_size = (len == (size_t)-1) ? st.st_size - saved : (off_t)len;
    off_t off = saved;
    ssize_t total = 0;
    while (total < file_size) {
      size_t want = (file_size - total) > CHUNK ? CHUNK : (size_t)(file_size - total);
      for (int i = 0; i < nouts; i++) {
        loff_t myoff = off;
        ssize_t c = try_cfr(infd, &myoff, outs[i], want);
        if (c < 0) {
          vwarn("copy_file_range duplication failed: %s\n", strerror(errno));
          goto dup_rw;
        }
        if ((size_t)c != want) {
          vwarn("short copy_file_range duplication, falling back\n");
          goto dup_rw;
        }
      }
      off += want;
      total += want;
    }
    if (lseek(infd, off, SEEK_SET) < 0)
      vwarn("lseek advance failed: %s\n", strerror(errno));
    free(buf);
    return total;
  }
dup_rw:
  if (nouts == 1) {
    int outfd = outs[0];
    for (;;) {
      if (ZCOPY == 3) {
        vinfo("zcopy: trying copy_file_range\n");
        ssize_t acc = 0;
        for (;;) {
          size_t want = (len == (size_t)-1) ? CHUNK : (len > CHUNK ? CHUNK : len);
          ssize_t c = try_cfr(infd, NULL, outfd, want);
          if (c > 0) {
            acc += c;
            if (len != (size_t)-1) {
              len -= c;
              if (len == 0) {
                free(buf);
                return acc;
              }
              continue;
            } else
              continue;
          }
          if (c == 0) {
            free(buf);
            return acc;
          }
          vwarn("copy_file_range failed: %s\n", strerror(errno));
          ZCOPY--;
          break;
        }
        continue;
      }
      if (ZCOPY == 2) {
        vinfo("zcopy: trying splice direct\n");
        ssize_t s = try_splice_direct_len(infd, outfd, len);
        if (s >= 0) {
          free(buf);
          return s;
        }
        vwarn("splice failed: %s\n", strerror(errno));
        ZCOPY--;
        continue;
      }
      if (ZCOPY == 1) {
        vinfo("zcopy: trying double-splice\n");
        if (!seekable) {
          vwarn("double-splice skipped: input not seekable\n");
          ZCOPY--;
          continue;
        }
        ssize_t s = try_double_splice_restore_len(infd, outfd, len);
        if (s >= 0) {
          free(buf);
          return s;
        }
        vwarn("double-splice failed: %s\n", strerror(errno));
        ZCOPY--;
        continue;
      }
      if (ZCOPY == 0) {
        vinfo("zcopy: falling back to read/write\n");
        ssize_t s = 0;
        while (len == (size_t)-1 || len > 0) {
          size_t wn = (len == (size_t)-1) ? CHUNK : (len > CHUNK ? CHUNK : len);
          ssize_t r = read(infd, buf, wn);
          if (r < 0) {
            if (errno == EINTR)
              continue;
            free(buf);
            return -1;
          }
          if (r == 0)
            break;
          if (write_all(outfd, buf, r) < 0) {
            free(buf);
            return -1;
          }
          s += r;
          if (len != (size_t)-1) {
            len -= r;
            if (len == 0)
              break;
          }
        }
        free(buf);
        return s;
      }
      break;
    }
    free(buf);
    die("zcopy: exhausted modes");
  } else {
    if (seekable) {
      if (lseek(infd, saved, SEEK_SET) < 0)
        vwarn("lseek back failed\n");
    }
    ssize_t t = try_user_copy_dup(infd, outs, nouts, buf);
    free(buf);
    return t;
  }
}
#include <stdarg.h>
ssize_t zcopy(int infd, int nouts, ...) {
  va_list ap;
  va_start(ap, nouts);
  int *outs = malloc(nouts * sizeof(int));
  if (!outs)
    die("malloc");
  for (int i = 0; i < nouts; i++)
    outs[i] = va_arg(ap, int);
  va_end(ap);
  ssize_t r = zcopy_len(infd, nouts, (size_t)-1, outs);
  free(outs);
  return r;
}
ssize_t zcopy_len_var(int infd, int nouts, size_t len, ...) {
  va_list ap;
  va_start(ap, len);
  int *outs = malloc(nouts * sizeof(int));
  if (!outs)
    die("malloc");
  for (int i = 0; i < nouts; i++)
    outs[i] = va_arg(ap, int);
  va_end(ap);
  ssize_t r = zcopy_len(infd, nouts, len, outs);
  free(outs);
  return r;
}
/* --- TAR helpers (minimal) --- */
struct posix_header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char chksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char pad[12];
};
void set_octal(char *p, size_t sz, unsigned long long v) {
  char b[64];
  snprintf(b, sizeof(b), "%0*llo", (int)(sz - 1), (unsigned long long)v);
  strncpy(p, b, sz - 1);
  p[sz - 1] = 0;
}
unsigned long long parse_octal(const char *p, size_t sz) {
  unsigned long long v = 0;
  size_t i = 0;
  while (i < sz && p[i] == ' ')
    i++;
  for (; i < sz && p[i] >= '0' && p[i] <= '7'; ++i)
    v = (v << 3) + (p[i] - '0');
  return v;
}
void write_zeros_fd(int outfd, size_t n) {
  char z[TAR_BLOCK];
  bool inited = false;
  if (!inited) {
    memset(z, 0, sizeof(z));
    inited = true;
  }
  while (n) {
    size_t w = n > TAR_BLOCK ? TAR_BLOCK : n;
    if (write_all(outfd, z, w) < 0)
      die("write zeros");
    n -= w;
  }
}
void ensure_parent_dirs(const char *path) {
  char *d = strdup(path);
  if (!d)
    return;
  char *dir = dirname(d);
  if (dir && strlen(dir) > 0) {
    char cur[PATH_MAX] = "";
    char tmp[PATH_MAX];
    strncpy(tmp, dir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char *p = tmp;
    if (*p == '/') {
      strcat(cur, "/");
      p++;
    }
    for (char *tok = strtok(p, "/"); tok; tok = strtok(NULL, "/")) {
      if (strlen(cur) + 1 + strlen(tok) + 1 >= sizeof(cur))
        break;
      if (strcmp(cur, "/") != 0 && cur[0] != 0)
        strcat(cur, "/");
      strcat(cur, tok);
      mkdir(cur, 0777);
    }
  }
  free(d);
}
void write_tar_header_full(int outfd, const char *path, const struct stat *st, char typeflag, const char *linkname) {
  struct posix_header h;
  memset(&h, 0, sizeof(h));
  if (strlen(path) < sizeof(h.name))
    strncpy(h.name, path, sizeof(h.name) - 1);
  else {
    const char *s = strrchr(path, '/');
    if (s && (s - path) < (int)sizeof(h.prefix) && strlen(s + 1) < sizeof(h.name)) {
      size_t L = s - path;
      memcpy(h.prefix, path, L);
      h.prefix[L] = 0;
      strncpy(h.name, s + 1, sizeof(h.name) - 1);
    } else
      die("path too long: %s", path);
  }
  set_octal(h.mode, sizeof(h.mode), st->st_mode & 07777);
  set_octal(h.uid, sizeof(h.uid), st->st_uid);
  set_octal(h.gid, sizeof(h.gid), st->st_gid);
  set_octal(h.size, sizeof(h.size), (typeflag == '0' && S_ISREG(st->st_mode)) ? (unsigned long long)st->st_size : 0ULL);
  set_octal(h.mtime, sizeof(h.mtime), (unsigned long long)st->st_mtime);
  h.typeflag = typeflag;
  if (linkname)
    strncpy(h.linkname, linkname, sizeof(h.linkname) - 1);
  memcpy(h.magic, "ustar", 5);
  h.version[0] = '0';
  h.version[1] = '0';
  memset(h.chksum, ' ', sizeof(h.chksum));
  unsigned long sum = 0;
  unsigned char *bb = (unsigned char *)&h;
  for (size_t i = 0; i < sizeof(h); ++i)
    sum += bb[i];
  char chk[16];
  snprintf(chk, sizeof(chk), "%06o", (unsigned)sum);
  memcpy(h.chksum, chk, 6);
  h.chksum[6] = 0;
  h.chksum[7] = ' ';
  if (write_all(outfd, &h, sizeof(h)) < 0)
    die("write header");
}
void tar_add_path(int outfd, const char *path) {
  struct stat st;
  if (lstat(path, &st) < 0) {
    vwarn("stat %s failed: %s\n", path, strerror(errno));
    return;
  }
  if (S_ISLNK(st.st_mode)) {
    char target[PATH_MAX];
    ssize_t len = readlink(path, target, sizeof(target) - 1);
    if (len < 0) {
      vwarn("readlink %s failed\n", path);
      return;
    }
    target[len] = 0;
    write_tar_header_full(outfd, path, &st, '2', target);
    return;
  }
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path);
    if (!d) {
      vwarn("opendir %s failed\n", path);
      return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
        continue;
      char c[PATH_MAX];
      snprintf(c, sizeof(c), "%s/%s", path, e->d_name);
      tar_add_path(outfd, c);
    }
    closedir(d);
    return;
  }
  if (S_ISREG(st.st_mode)) {
    write_tar_header_full(outfd, path, &st, '0', NULL);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      vwarn("open %s failed\n", path);
      return;
    }
    if (zcopy_len_var(fd, 1, (size_t)st.st_size, outfd) < 0)
      vwarn("copy failed\n");
    close(fd);
    off_t pad = (TAR_BLOCK - (st.st_size % TAR_BLOCK)) % TAR_BLOCK;
    if (pad)
      write_zeros_fd(outfd, pad);
    return;
  }
  vwarn("skip unsupported: %s\n", path);
}
void tar_create(const char *outpath, int nfiles, char **files) {
  int outfd = open(outpath, O_CREAT | O_TRUNC | O_WRONLY, 0666);
  if (outfd < 0) {
    vwarn("open %s failed\n", outpath);
    return;
  }
  for (int i = 0; i < nfiles; i++)
    tar_add_path(outfd, files[i]);
  write_zeros_fd(outfd, TAR_BLOCK * 2);
  close(outfd);
  vinfo("tar created: %s\n", outpath);
}
void tar_extract(const char *inpath) {
  int infd = open(inpath, O_RDONLY);
  if (infd < 0) {
    vwarn("open %s failed\n", inpath);
    return;
  }
  while (1) {
    struct posix_header h;
    ssize_t r = read(infd, &h, sizeof(h));
    if (r < 0) {
      vwarn("read tar header failed: %s\n", strerror(errno));
      break;
    }
    if (r == 0)
      break;
    if (r != sizeof(h)) {
      vwarn("short tar header\n");
      break;
    }
    bool allz = true;
    for (size_t i = 0; i < sizeof(h); ++i)
      if (((unsigned char *)&h)[i]) {
        allz = false;
        break;
      }
    if (allz)
      break;
    char name[PATH_MAX];
    if (h.prefix[0])
      snprintf(name, sizeof(name), "%s/%s", h.prefix, h.name);
    else
      snprintf(name, sizeof(name), "%s", h.name);
    unsigned long long size = parse_octal(h.size, sizeof(h.size));
    size_t modebits = (size_t)parse_octal(h.mode, sizeof(h.mode));
    time_t mtime = (time_t)parse_octal(h.mtime, sizeof(h.mtime));
    if (h.typeflag == '2') {
      ensure_parent_dirs(name);
      if (symlink(h.linkname, name) < 0 && errno != EEXIST)
        vwarn("symlink %s failed\n", name);
      struct utimbuf t = {mtime, mtime};
      utime(name, &t);
      continue;
    } else if (h.typeflag == '5') {
      if (mkdir(name, modebits) < 0 && errno != EEXIST)
        vwarn("mkdir %s failed\n", name);
      struct utimbuf t = {mtime, mtime};
      utime(name, &t);
      continue;
    } else if (h.typeflag == '0' || h.typeflag == '\0') {
      ensure_parent_dirs(name);
      int outfd = open(name, O_CREAT | O_TRUNC | O_WRONLY, modebits & 07777);
      if (outfd < 0) {
        vwarn("create %s failed\n", name); /* skip data */
        off_t pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        if (lseek(infd, size + pad, SEEK_CUR) < 0) {
          char buf[4096];
          off_t left = size + pad;
          while (left > 0) {
            ssize_t rr = read(infd, buf, left > (off_t)sizeof(buf) ? sizeof(buf) : left);
            if (rr <= 0)
              break;
            left -= rr;
          }
        }
        continue;
      }
      off_t rem = size;
      while (rem > 0) {
        size_t want = rem > CHUNK ? CHUNK : rem;
        if (zcopy_len_var(infd, 1, want, outfd) < 0) {
          vwarn("extract copy failed\n");
          break;
        }
        rem -= want;
      }
      close(outfd);
      struct utimbuf t = {mtime, mtime};
      utime(name, &t);
      off_t pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
      if (pad) {
        if (lseek(infd, pad, SEEK_CUR) < 0) {
          char junk[512];
          off_t left = pad;
          while (left > 0) {
            ssize_t rr = read(infd, junk, left > (off_t)sizeof(junk) ? sizeof(junk) : left);
            if (rr <= 0)
              break;
            left -= rr;
          }
        }
      }
      continue;
    } else {
      unsigned long long size2 = parse_octal(h.size, sizeof(h.size));
      off_t pad = (TAR_BLOCK - (size2 % TAR_BLOCK)) % TAR_BLOCK;
      off_t skip = size2 + pad;
      if (lseek(infd, skip, SEEK_CUR) < 0) {
        char buf[4096];
        off_t left = skip;
        while (left > 0) {
          ssize_t rr = read(infd, buf, left > (off_t)sizeof(buf) ? sizeof(buf) : left);
          if (rr <= 0)
            break;
          left -= rr;
        }
      }
      vwarn("skipped unsupported entry %s\n", name);
    }
  }
  close(infd);
  vinfo("tar extracted: %s\n", inpath);
}
/* --- RAID helpers (create/unraid) --- */
bool is_devnull(const char *p) { return p && strcmp(p, "/dev/null") == 0; }
void repair_block_write(const char *fname, off_t off, const unsigned char *buf, size_t n) {
  if (!fname)
    return;
  if (is_devnull(fname))
    return;
  int fd = open(fname, O_WRONLY);
  if (fd < 0) {
    vwarn("repair open %s failed: %s\n", fname, strerror(errno));
    return;
  }
  if (lseek(fd, off, SEEK_SET) < 0)
    vwarn("repair lseek %s failed: %s\n", fname, strerror(errno));
  if (write_all(fd, buf, n) < 0)
    vwarn("repair write %s failed: %s\n", fname, strerror(errno));
  close(fd);
  vinfo("repaired block on %s\n", fname);
}
/* create/unraid simplified with Z3 */
void raid_create_from_fd(int infd, char **outs, int nouts, int RAID, int FORCE, int CHECK, bool plan_only) {
  if (nouts <= 0)
    return;
  if (RAID == 0) {
    if (plan_only) {
      return;
    }
    int *fds = malloc(nouts * sizeof(int));
    for (int i = 0; i < nouts; i++)
      fds[i] = open(outs[i], O_WRONLY | O_CREAT | (FORCE ? O_TRUNC : 0), 0644);
    unsigned char *buf = malloc(BLOCK);
    ssize_t r;
    long blk = 0;
    while ((r = read(infd, buf, BLOCK)) > 0) {
      int idx = blk % nouts;
      if (fds[idx] >= 0)
        if (write_all(fds[idx], buf, r) < 0)
          vwarn("write failed\n");
      blk++;
    }
    for (int i = 0; i < nouts; i++)
      if (fds[i] >= 0)
        close(fds[i]);
    free(buf);
    free(fds);
    return;
  }
  if (RAID == 1) {
    if (plan_only) {
      return;
    }
    int *fds = malloc(nouts * sizeof(int));
    for (int i = 0; i < nouts; i++)
      fds[i] = open(outs[i], O_WRONLY | O_CREAT | (FORCE ? O_TRUNC : 0), 0644);
    unsigned char *buf = malloc(CHUNK);
    ssize_t r;
    while ((r = read(infd, buf, CHUNK)) > 0) {
      for (int i = 0; i < nouts; i++)
        if (fds[i] >= 0)
          if (write_all(fds[i], buf, r) < 0)
            vwarn("write failed\n");
    }
    for (int i = 0; i < nouts; i++)
      if (fds[i] >= 0)
        close(fds[i]);
    free(buf);
    free(fds);
    return;
  }
  if (RAID == 10) {
    if (plan_only) {
      return;
    }
    if (nouts % 2) {
      vwarn("raid10 requires even outputs\n");
      return;
    }
    int *fds = malloc(nouts * sizeof(int));
    for (int i = 0; i < nouts; i++)
      fds[i] = open(outs[i], O_WRONLY | O_CREAT | (FORCE ? O_TRUNC : 0), 0644);
    unsigned char *buf = malloc(BLOCK);
    ssize_t r;
    long blk = 0;
    while ((r = read(infd, buf, BLOCK)) > 0) {
      int pair = (blk % (nouts / 2)) * 2;
      if (fds[pair] >= 0)
        if (write_all(fds[pair], buf, r) < 0)
          vwarn("write failed\n");
      if (fds[pair + 1] >= 0)
        if (write_all(fds[pair + 1], buf, r) < 0)
          vwarn("write failed\n");
      blk++;
    }
    for (int i = 0; i < nouts; i++)
      if (fds[i] >= 0)
        close(fds[i]);
    free(buf);
    free(fds);
    return;
  }
  if (RAID == 5 || RAID == 6 || RAID == 3) {
    if ((RAID == 5 && nouts < 3) || (RAID == 6 && nouts < 4) || (RAID == 3 && nouts < 5)) {
      vwarn("insufficient outputs\n");
      return;
    }
    if (plan_only) {
      return;
    }
    int *fds = malloc(nouts * sizeof(int));
    for (int i = 0; i < nouts; i++)
      fds[i] = open(outs[i], O_WRONLY | O_CREAT | (FORCE ? O_TRUNC : 0), 0644);
    size_t data_disks = (RAID == 5 ? nouts - 1 : (RAID == 6 ? nouts - 2 : nouts - 3));
    unsigned char *bufs = malloc(nouts * BLOCK);
    unsigned char *p = malloc(BLOCK), *q = malloc(BLOCK), *rpar = malloc(BLOCK);
    if (!bufs || !p || !q || !rpar)
      die("malloc");
    gf_init();
    for (;;) {
      memset(p, 0, BLOCK);
      memset(q, 0, BLOCK);
      memset(rpar, 0, BLOCK);
      int any = 0;
      for (size_t i = 0; i < data_disks; i++) {
        ssize_t got = read(infd, bufs + i * BLOCK, BLOCK);
        if (got < 0) {
          if (errno == EINTR) {
            --i;
            continue;
          }
          vwarn("read failed\n");
          got = 0;
        }
        if (got == 0)
          memset(bufs + i * BLOCK, 0, BLOCK);
        else
          any = 1;
        xor_blocks(p, bufs + i * BLOCK, BLOCK);
        for (size_t j = 0; j < (size_t)BLOCK; j++) {
          unsigned char a = bufs[i * BLOCK + j];
          q[j] ^= gf_mul(a, gf_exp[i]);
          rpar[j] ^= gf_mul(a, gf_exp[(2 * i) % 255]);
        }
      }
      if (!any)
        break;
      for (size_t i = 0; i < data_disks; i++)
        if (fds[i] >= 0)
          if (write_all(fds[i], bufs + i * BLOCK, BLOCK) < 0)
            vwarn("write failed\n");
      if (RAID == 5) {
        if (fds[nouts - 1] >= 0)
          if (write_all(fds[nouts - 1], p, BLOCK) < 0)
            vwarn("write failed\n");
      } else if (RAID == 6) {
        if (fds[nouts - 2] >= 0)
          if (write_all(fds[nouts - 2], p, BLOCK) < 0)
            vwarn("write failed\n");
        if (fds[nouts - 1] >= 0)
          if (write_all(fds[nouts - 1], q, BLOCK) < 0)
            vwarn("write failed\n");
      } else {
        if (fds[nouts - 3] >= 0)
          if (write_all(fds[nouts - 3], p, BLOCK) < 0)
            vwarn("write failed\n");
        if (fds[nouts - 2] >= 0)
          if (write_all(fds[nouts - 2], q, BLOCK) < 0)
            vwarn("write failed\n");
        if (fds[nouts - 1] >= 0)
          if (write_all(fds[nouts - 1], rpar, BLOCK) < 0)
            vwarn("write failed\n");
      }
    }
    for (int i = 0; i < nouts; i++)
      if (fds[i] >= 0)
        close(fds[i]);
    free(bufs);
    free(p);
    free(q);
    free(rpar);
    free(fds);
    return;
  }
  vwarn("unsupported raid mode\n");
}
/* Solve linear system Ax=b over GF(2^8) (square m x m). A and b are unsigned
 * char arrays */
int gf_solve(unsigned char *A, unsigned char *b, unsigned char *x, int m) {
  for (int i = 0; i < m; i++) {
    int pivot = -1;
    for (int r = i; r < m; r++)
      if (A[r * m + i] != 0) {
        pivot = r;
        break;
      }
    if (pivot == -1)
      return -1;
    if (pivot != i) {
      for (int c = i; c < m; c++) {
        unsigned char tmp = A[i * m + c];
        A[i * m + c] = A[pivot * m + c];
        A[pivot * m + c] = tmp;
      }
      unsigned char tb = b[i];
      b[i] = b[pivot];
      b[pivot] = tb;
    }
    unsigned char inv = gf_div(1, A[i * m + i]);
    for (int c = i; c < m; c++)
      A[i * m + c] = gf_mul(A[i * m + c], inv);
    b[i] = gf_mul(b[i], inv);
    for (int r = 0; r < m; r++) {
      if (r == i)
        continue;
      unsigned char factor = A[r * m + i];
      if (factor == 0)
        continue;
      for (int c = i; c < m; c++) {
        unsigned char t = gf_mul(factor, A[i * m + c]);
        A[r * m + c] ^= t;
      }
      b[r] ^= gf_mul(factor, b[i]);
    }
  }
  for (int i = 0; i < m; i++)
    x[i] = b[i];
  return 0;
}
/* unraid (simplified; tries to repair when possible) */
int unraid_from_parts_to_fd(const char *outfile, char **ins, int nins, int RAID, int FORCE, int CHECK, bool plan_only) {
  if (nins <= 0)
    return 0;
  if (plan_only) {
    return 0;
  }
  int *fds = malloc(nins * sizeof(int));
  int *repair = malloc(nins * sizeof(int));
  for (int i = 0; i < nins; i++) {
    repair[i] = -1;
    fds[i] = open(ins[i], O_RDONLY);
    if (fds[i] < 0)
      repair[i] = open(ins[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }
  int misscnt = 0;
  for (int i = 0; i < nins; i++)
    if (fds[i] < 0)
      misscnt++;
  if (RAID == 0) {
    if (misscnt) {
      vwarn("raid0 cannot reconstruct with missing parts\n");
      free(fds);
      free(repair);
      return 1;
    }
    int out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
      vwarn("create out failed\n");
      free(fds);
      free(repair);
      return 1;
    }
    unsigned char *buf = malloc(BLOCK);
    for (;;) {
      for (int i = 0; i < nins; i++) {
        ssize_t r = read(fds[i], buf, BLOCK);
        if (r < 0) {
          if (errno == EINTR) {
            i--;
            continue;
          }
          r = 0;
        }
        if (r == 0)
          goto done0;
        if (write_all(out, buf, r) < 0) {
          vwarn("write out failed\n");
        }
      }
    }
  done0:
    for (int i = 0; i < nins; i++)
      if (fds[i] >= 0)
        close(fds[i]);
    if (out >= 0)
      close(out);
    free(buf);
    free(fds);
    free(repair);
    return 0;
  }
  if (RAID == 1) {
    int good = -1;
    for (int i = 0; i < nins; i++)
      if (fds[i] >= 0) {
        good = i;
        break;
      }
    if (good < 0) {
      vwarn("all mirrors missing\n");
      free(fds);
      free(repair);
      return 1;
    }
    int out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
      vwarn("create out failed\n");
      free(fds);
      free(repair);
      return 1;
    }
    unsigned char *buf = malloc(CHUNK);
    ssize_t r;
    while ((r = read(fds[good], buf, CHUNK)) > 0) {
      if (write_all(out, buf, r) < 0)
        vwarn("write out failed\n");
      for (int i = 0; i < nins; i++)
        if (fds[i] < 0 && repair[i] >= 0)
          if (write_all(repair[i], buf, r) < 0)
            vwarn("repair %s failed\n", ins[i]);
    }
    for (int i = 0; i < nins; i++) {
      if (fds[i] >= 0)
        close(fds[i]);
      if (repair[i] >= 0)
        close(repair[i]);
    }
    if (out >= 0)
      close(out);
    free(buf);
    free(fds);
    free(repair);
    return 0;
  }
  if (RAID == 10) {
    if (nins % 2) {
      vwarn("raid10 requires even count\n");
      free(fds);
      free(repair);
      return 1;
    }
    int out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
      vwarn("create out failed\n");
      free(fds);
      free(repair);
      return 1;
    }
    unsigned char *buf = malloc(BLOCK);
    for (int pair = 0; pair < nins; pair += 2) {
      int a = pair, b = pair + 1;
      int src = (fds[a] >= 0) ? a : ((fds[b] >= 0) ? b : -1);
      if (src < 0) {
        vwarn("lost mirror pair %d\n", pair / 2);
        free(buf);
        free(fds);
        free(repair);
        close(out);
        return 1;
      }
      lseek(fds[src], 0, SEEK_SET);
      ssize_t r;
      while ((r = read(fds[src], buf, BLOCK)) > 0) {
        if (write_all(out, buf, r) < 0)
          vwarn("write out failed\n");
        if (fds[a] < 0 && repair[a] >= 0)
          if (write_all(repair[a], buf, r) < 0)
            vwarn("repair %s failed\n", ins[a]);
        if (fds[b] < 0 && repair[b] >= 0)
          if (write_all(repair[b], buf, r) < 0)
            vwarn("repair %s failed\n", ins[b]);
      }
    }
    for (int i = 0; i < nins; i++) {
      if (fds[i] >= 0)
        close(fds[i]);
      if (repair[i] >= 0)
        close(repair[i]);
    }
    close(out);
    free(buf);
    free(fds);
    free(repair);
    return 0;
  }
  /* parity cases (5/6/3) use same solver as earlier, attempt to reconstruct up
   * to parity */
  if (RAID == 5 || RAID == 6 || RAID == 3) {
    if ((RAID == 5 && nins < 3) || (RAID == 6 && nins < 4) || (RAID == 3 && nins < 5)) {
      vwarn("insufficient parts\n");
      free(fds);
      free(repair);
      return 1;
    }
    gf_init();
    unsigned char *bufs = malloc(nins * BLOCK);
    int *missing = calloc(nins, sizeof(int));
    for (int i = 0; i < nins; i++)
      if (fds[i] < 0)
        missing[i] = 1;
    int miss = 0;
    for (int i = 0; i < nins; i++)
      if (missing[i])
        miss++;
    int parity = (RAID == 5 ? 1 : (RAID == 6 ? 2 : 3));
    if (miss > parity) {
      vwarn("too many missing parts\n");
      free(bufs);
      free(missing);
      free(fds);
      free(repair);
      return 1;
    }
    int out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
      vwarn("create out failed\n");
      free(bufs);
      free(missing);
      free(fds);
      free(repair);
      return 1;
    }
    int stripe = 0;
    while (1) {
      int any = 0;
      for (int i = 0; i < nins; i++) {
        if (fds[i] < 0)
          memset(bufs + i * BLOCK, 0, BLOCK);
        else {
          ssize_t r = read(fds[i], bufs + i * BLOCK, BLOCK);
          if (r < 0) {
            if (errno == EINTR) {
              i--;
              continue;
            }
            r = 0;
          }
          if (r == 0)
            memset(bufs + i * BLOCK, 0, BLOCK);
          else if (i < nins - parity)
            any = 1;
        }
      }
      if (!any)
        break;
      if (miss == 0 && CHECK) {
        unsigned char *P = malloc(BLOCK), *Q = malloc(BLOCK), *R = malloc(BLOCK);
        memset(P, 0, BLOCK);
        memset(Q, 0, BLOCK);
        memset(R, 0, BLOCK);
        for (int i = 0; i < nins - parity; i++) {
          xor_blocks(P, bufs + i * BLOCK, BLOCK);
          for (size_t j = 0; j < BLOCK; j++) {
            unsigned char a = bufs[i * BLOCK + j];
            Q[j] ^= gf_mul(a, gf_exp[i]);
            R[j] ^= gf_mul(a, gf_exp[(2 * i) % 255]);
          }
        }
        if (parity >= 1 && memcmp(P, bufs + (nins - parity) * BLOCK, BLOCK) != 0)
          repair_block_write(ins[nins - parity], (off_t)stripe * BLOCK, P, BLOCK);
        if (parity >= 2 && memcmp(Q, bufs + (nins - parity + 1) * BLOCK, BLOCK) != 0)
          repair_block_write(ins[nins - parity + 1], (off_t)stripe * BLOCK, Q, BLOCK);
        if (parity >= 3 && memcmp(R, bufs + (nins - 1) * BLOCK, BLOCK) != 0)
          repair_block_write(ins[nins - 1], (off_t)stripe * BLOCK, R, BLOCK);
        free(P);
        free(Q);
        free(R);
      }
      if (miss > 0) {
        int only_parity_missing = 1;
        for (int i = 0; i < nins - parity; i++)
          if (missing[i])
            only_parity_missing = 0;
        if (only_parity_missing) {
          unsigned char *P = malloc(BLOCK), *Q = malloc(BLOCK), *R = malloc(BLOCK);
          memset(P, 0, BLOCK);
          memset(Q, 0, BLOCK);
          memset(R, 0, BLOCK);
          for (int i = 0; i < nins - parity; i++) {
            xor_blocks(P, bufs + i * BLOCK, BLOCK);
            for (size_t j = 0; j < BLOCK; j++) {
              unsigned char a = bufs[i * BLOCK + j];
              Q[j] ^= gf_mul(a, gf_exp[i]);
              R[j] ^= gf_mul(a, gf_exp[(2 * i) % 255]);
            }
          }
          if (parity >= 1 && missing[nins - parity]) {
            if (repair[nins - parity] >= 0)
              repair_block_write(ins[nins - parity], (off_t)stripe * BLOCK, P, BLOCK);
            memcpy(bufs + (nins - parity) * BLOCK, P, BLOCK);
          }
          if (parity >= 2 && missing[nins - parity + 1]) {
            if (repair[nins - parity + 1] >= 0)
              repair_block_write(ins[nins - parity + 1], (off_t)stripe * BLOCK, Q, BLOCK);
            memcpy(bufs + (nins - parity + 1) * BLOCK, Q, BLOCK);
          }
          if (parity >= 3 && missing[nins - 1]) {
            if (repair[nins - 1] >= 0)
              repair_block_write(ins[nins - 1], (off_t)stripe * BLOCK, R, BLOCK);
            memcpy(bufs + (nins - 1) * BLOCK, R, BLOCK);
          }
          free(P);
          free(Q);
          free(R);
        } else {
          int data_disks = nins - parity;
          int unknown_idx[parity];
          int m = 0;
          for (int i = 0; i < data_disks; i++)
            if (missing[i])
              unknown_idx[m++] = i;
          if (m > parity) {
            vwarn("cannot solve: too many missing data\n");
            free(bufs);
            free(missing);
            free(fds);
            free(repair);
            close(out);
            return 1;
          }
          if (m > 0) {
            unsigned char *A = malloc(parity * m);
            unsigned char *b = malloc(parity);
            unsigned char *Asq = malloc(m * m);
            unsigned char *bsq = malloc(m);
            unsigned char *sol = malloc(m);
            for (size_t byte = 0; byte < (size_t)BLOCK; ++byte) {
              for (int pr = 0; pr < parity; pr++) {
                unsigned char rhs = 0;
                for (int i = 0; i < data_disks; i++) {
                  unsigned char coef = (pr == 0) ? 1 : gf_exp[((pr - 1) * i) % 255];
                  if (!missing[i])
                    rhs ^= gf_mul(coef, bufs[i * BLOCK + byte]);
                }
                unsigned char parity_val = bufs[(nins - parity + pr) * BLOCK + byte];
                b[pr] = parity_val ^ rhs;
              }
              for (int pr = 0; pr < parity; pr++) {
                for (int col = 0; col < m; col++) {
                  int di = unknown_idx[col];
                  unsigned char coef = (pr == 0) ? 1 : gf_exp[((pr - 1) * di) % 255];
                  A[pr * m + col] = coef;
                }
              }
              for (int r = 0; r < m; r++) {
                for (int c = 0; c < m; c++)
                  Asq[r * m + c] = A[r * m + c];
                bsq[r] = b[r];
              }
              if (gf_solve(Asq, bsq, sol, m) != 0) {
                vwarn("linear solve failed at stripe %d\n", stripe);
                free(A);
                free(b);
                free(Asq);
                free(bsq);
                free(sol);
                free(bufs);
                free(missing);
                free(fds);
                free(repair);
                close(out);
                return 1;
              }
              for (int col = 0; col < m; col++)
                bufs[unknown_idx[col] * BLOCK + byte] = sol[col];
            }
            for (int col = 0; col < m; col++) {
              int di = unknown_idx[col];
              if (repair[di] >= 0)
                repair_block_write(ins[di], (off_t)stripe * BLOCK, bufs + di * BLOCK, BLOCK);
            }
            free(A);
            free(b);
            free(Asq);
            free(bsq);
            free(sol);
          }
        }
      }
      int data_disks = nins - parity;
      for (int i = 0; i < data_disks; i++) {
        if (write_all(out, bufs + i * BLOCK, BLOCK) < 0)
          vwarn("write out failed\n");
      }
      stripe++;
    }
    for (int i = 0; i < nins; i++) {
      if (fds[i] >= 0)
        close(fds[i]);
      if (repair[i] >= 0)
        close(repair[i]);
    }
    /* Truncate output file to correct length */
    off_t total_data_bytes = (off_t)stripe * BLOCK * (nins - parity);
    /* Find actual input size from first available data disk */
    off_t actual_size = 0;
    for (int i = 0; i < nins - parity; i++) {
      if (access(ins[i], F_OK) == 0) {
        struct stat st;
        if (stat(ins[i], &st) == 0) {
          off_t this_disk_blocks = (st.st_size + BLOCK - 1) / BLOCK;
          off_t this_disk_data = this_disk_blocks * (nins - parity);
          if (actual_size == 0 || this_disk_data < total_data_bytes) {
            /* Last stripe might be partial */
            off_t last_stripe_bytes = st.st_size - (this_disk_blocks - 1) * BLOCK;
            actual_size = (this_disk_blocks - 1) * BLOCK * (nins - parity) + last_stripe_bytes * (nins - parity);
          }
          break;
        }
      }
    }
    if (actual_size > 0 && actual_size < total_data_bytes)
      ftruncate(out, actual_size);
    close(out);
    free(bufs);
    free(fds);
    free(repair);
    free(missing);
    return 0;
  }
  vwarn("unsupported raid mode\n");
  free(fds);
  free(repair);
  return 1;
}
/* --- helpers --- */
bool is_regular_file(const char *path) {
  struct stat st;
  if (stat(path, &st) < 0)
    return false;
  return S_ISREG(st.st_mode);
}
/* --- global CHECK failure flag --- */
int GLOBAL_CHECK_FAILED = 0;
/* --- commands --- */
int do_cat(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: (un)cat <file> <chunk>...\n");
    return 2;
  }
  const char *file = argv[1];
  char **chunks = &argv[2];
  int nch = argc - 2;
  if (UNDO) {
    /* build file from chunks */
    if (!FORCE) {
      /* CHECK missing chunk files; if missing and CHECK -> report; else warn */
      for (int i = 0; i < nch; i++)
        if (!is_regular_file(chunks[i])) {
          vwarn("missing chunk %s\n", chunks[i]);
          if (CHECK)
            GLOBAL_CHECK_FAILED = 1;
          return GLOBAL_CHECK_FAILED;
        }
    }
    int outfd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (outfd < 0) {
      vwarn("create %s failed\n", file);
      return 1;
    }
    for (int i = 0; i < nch; i++) {
      int infd = open(chunks[i], O_RDONLY);
      if (infd < 0) {
        vwarn("open chunk %s failed\n", chunks[i]);
        close(outfd);
        return 1;
      }
      vinfo("copy chunk %s -> %s\n", chunks[i], file);
      if (zcopy(infd, 1, outfd) < 0) {
        vwarn("copy chunk %s failed\n", chunks[i]);
        close(infd);
        close(outfd);
        return 1;
      }
      close(infd);
    }
    close(outfd);
    return 0;
  } else {
    /* split */
    struct stat st;
    if (stat(file, &st) < 0) {
      vwarn("stat %s failed\n", file);
      return 1;
    }
    off_t size = st.st_size;
    off_t base = size / nch;
    off_t rem = size % nch;
    for (int i = 0; i < nch; i++) {
      off_t want = base + (i < rem ? 1 : 0);
      bool chunk_exists = is_regular_file(chunks[i]);
      if (chunk_exists && !FORCE && !CHECK) {
        vinfo("skip existing chunk %s\n", chunks[i]);
        continue;
      }
      vinfo("%s chunk %s from %s offset %lld len %lld\n", (chunk_exists ? (CHECK ? "check/overwrite" : "overwrite") : "create"), chunks[i], file, (long long)(i * base + (i < rem ? i : rem)), (long long)want);
      int outfd = open(chunks[i], O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (outfd < 0) {
        vwarn("create %s failed\n", chunks[i]);
        continue;
      }
      int infd = open(file, O_RDONLY);
      if (infd < 0) {
        vwarn("open %s failed\n", file);
        close(outfd);
        continue;
      }
      off_t pos = (off_t)(i * base + (i < rem ? i : rem));
      if (lseek(infd, pos, SEEK_SET) < 0)
        vwarn("lseek failed: %s\n", strerror(errno));
      if (zcopy_len_var(infd, 1, want, outfd) < 0)
        vwarn("copy chunk failed\n");
      close(infd);
      close(outfd);
    }
    return 0;
  }
}
int do_tee(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: tee <file>...\n");
    return 2;
  }
  int first_found = -1;
  for (int i = 1; i < argc; i++)
    if (is_regular_file(argv[i])) {
      first_found = i;
      break;
    }
  if (first_found >= 0) {
    for (int i = 1; i < argc; i++) {
      if (i == first_found)
        continue;
      if (!FORCE && is_regular_file(argv[i]) && !CHECK) {
        vinfo("skip existing %s\n", argv[i]);
        continue;
      }
      int infd = open(argv[first_found], O_RDONLY);
      if (infd < 0) {
        vwarn("open %s failed\n", argv[first_found]);
        continue;
      }
      int outfd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (outfd < 0) {
        vwarn("open %s failed\n", argv[i]);
        close(infd);
        continue;
      }
      vinfo("copy %s -> %s\n", argv[first_found], argv[i]);
      if (zcopy(infd, 1, outfd) < 0)
        vwarn("copy to %s failed\n", argv[i]);
      close(infd);
      close(outfd);
    }
    return 0;
  } else {
    vwarn("no regular source file found; program won't read stdin by default\n");
    return 1;
  }
}
int do_tar(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: (un)tar <tarfile> <files...> (no files => extract)\n");
    return 2;
  }
  if (UNDO) {
    tar_extract(argv[1]);
    return 0;
  } else {
    tar_create(argv[1], argc - 2, &argv[2]);
    return 0;
  }
}
/* RAID driver: if file exists and not UNDO -> create parts; else unraid */
int do_raid(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: (un)raid(0/1/2/3/5/6/10) <file> <part0> <part1> ...\n");
    return 2;
  }
  const char *file = argv[1];
  char **parts = &argv[2];
  int nparts = argc - 2;
  if (UNDO) {
    return unraid_from_parts_to_fd(file, parts, nparts, RAID, FORCE, CHECK, false);
  } else {
    int infd = open(file, O_RDONLY);
    if (infd < 0) {
      vwarn("open %s failed\n", file);
      return 1;
    }
    raid_create_from_fd(infd, parts, nparts, RAID, FORCE, CHECK, false);
    close(infd);
    return 0;
  }
}
/* md5sum: compute md5 for each file; if CHECK and md5file exists, compare and
 * set GLOBAL_CHECK_FAILED */
int do_md5sum(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: (un)md5sum <md5file> <file>...\n");
    return 2;
  }
  const char *md5file = argv[1];
  /* read expected sums if CHECK and file exists */
  FILE *exp = NULL;
  if (CHECK && access(md5file, F_OK) == 0)
    exp = fopen(md5file, "r");
  FILE *out = fopen(md5file, CHECK ? "r" : "w+");
  if (!out) {
    vwarn("open %s failed: %s\n", md5file, strerror(errno)); /* but continue printing to stdout */
    out = stdout;
  }
  /* If CHECK and exp opened, we'll compare; otherwise write results */
  char hex[33];
  for (int i = 2; i < argc; i++) {
    const char *fn = argv[i];
    int fd = open(fn, O_RDONLY);
    if (fd < 0) {
      vwarn("open %s failed\n", fn);
      if (CHECK)
        GLOBAL_CHECK_FAILED = 1;
      continue;
    }
    MD5_CTX ctx;
    md5_init(&ctx);
    unsigned char buf[CHUNK];
    for (;;) {
      ssize_t r = read(fd, buf, sizeof(buf));
      if (r < 0) {
        if (errno == EINTR)
          continue;
        vwarn("read %s failed\n", fn);
        close(fd);
        if (CHECK)
          GLOBAL_CHECK_FAILED = 1;
        goto nextfile;
      }
      if (r == 0)
        break;
      md5_update(&ctx, buf, r);
    }
    unsigned char d[16];
    md5_final(&ctx, d);
    for (int k = 0; k < 16; k++)
      sprintf(hex + k * 2, "%02x", d[k]);
    hex[32] = 0;
    if (CHECK && exp) {
      int found_mismatch = 0;
      /* look for corresponding line in exp */
      rewind(exp);
      char line[4096];
      bool found = false;
      while (fgets(line, sizeof(line), exp)) {
        /* expecting "<md5> <filename>\n" */
        char *p = strchr(line, ' ');
        if (!p)
          continue;
        *p = 0;
        char *fname = p + 2;
        char *nl = strchr(fname, '\n');
        if (nl)
          *nl = 0;
        if (strcmp(fname, fn) == 0) {
          found = true;
          if (strcmp(line, hex) != 0) {
            vwarn("md5 mismatch %s: expected %s, got %s\n", fn, line, hex);
            GLOBAL_CHECK_FAILED = 1;
            found_mismatch = 1;
          } else
            vinfo("md5 ok %s\n", fn);
          break;
        }
      }
      if (!found && !found_mismatch) {
        vwarn("no entry for %s in %s\n", fn, md5file);
        GLOBAL_CHECK_FAILED = 1;
      }
    } else {
      fprintf(out, "%s %s\n", hex, fn);
    }
  nextfile:
    close(fd);
  }
  if (exp)
    fclose(exp);
  if (out && out != stdout)
    fclose(out);
  return GLOBAL_CHECK_FAILED;
}
/* Main dispatcher */
int main(int argc, char **argv) {
  ZCOPY = env("ZCOPY", 2);
  RAID = env("RAID", 0);
  BLOCK = env("BLOCK", 4096);
  FORCE = env("FORCE", 0);
  CHECK = env("CHECK", 0);
  UNDO = env("UNDO", 0);
  VERBOSE = env("VERBOSE", 2);
  argv[0] = strrchr(argv[0], '/') ?: argv[0];
  if (strstr(argv[0], "un"))
    UNDO = 1;
  if (strstr(argv[0], "10"))
    RAID = 10;
  else if (strstr(argv[0], "0"))
    RAID = 0;
  else if (strstr(argv[0], "1"))
    RAID = 1;
  else if (strstr(argv[0], "2"))
    RAID = 2;
  else if (strstr(argv[0], "3"))
    RAID = 3;
  else if (strstr(argv[0], "5"))
    RAID = 5;
  else if (strstr(argv[0], "6"))
    RAID = 6;
  vinfo("ZCOPY=%d RAID=%d BLOCK=%d FORCE=%d CHECK=%d UNDO=%d VERBOSE=%d\n", ZCOPY, RAID, BLOCK, FORCE, CHECK, UNDO, VERBOSE);
  if (strstr(argv[0], "cat"))
    return do_cat(argc, argv);
  if (strstr(argv[0], "tee"))
    return do_tee(argc, argv);
  if (strstr(argv[0], "tar"))
    return do_tar(argc, argv);
  if (strstr(argv[0], "md5sum"))
    return do_md5sum(argc, argv);
  if (strstr(argv[0], "raid"))
    return do_raid(argc, argv);
  fprintf(stderr, "unknown command (symlink this program to "
                  "(un)(cat/tee/tar/md5sum/raid(0/1/2/3/5/6/10))\n");
  return 2;
}