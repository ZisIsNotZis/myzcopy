#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
const char MSG[] = "HTTP/1.1 200 OK\r\nContent-Length:10\r\n\r\nHelloWorld";
const int N = 2048;

static int wrn(const char* s, int res) {
    if (res < 0) fprintf(stderr, "%s %s\n", s, strerror(-res));
    return res;
}

static int err(const char* s, int res) {
    if (wrn(s, res) < 0) exit(1);
    return res;
}

static struct io_uring_sqe* getsq(struct io_uring* ring, unsigned long job) {
    struct io_uring_sqe* sq = io_uring_get_sqe(ring);
    if (!sq) err("sq", -1);
    sq->user_data = job;
    return sq;
}

int main(int argc, char** argv) {
    int fd = err("socket", socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0));
    int tmp = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &tmp, sizeof(tmp));
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family = AF_INET;
    adr.sin_addr.s_addr = INADDR_ANY;
    adr.sin_port = htons(8080);
    err("bind", bind(fd, (struct sockaddr*)&adr, sizeof(adr)));
    err("listen", listen(fd, N));
    struct io_uring ring;
    struct io_uring_params prm;
    memset(&prm, 0, sizeof(prm));
    prm.flags = IORING_SETUP_SQPOLL;
    prm.sq_thread_idle = 20;
    err("init", io_uring_queue_init_params(N * 4, &ring, &prm));
    err("reg", io_uring_register_files_sparse(&ring, N));
    struct io_uring_sqe* sq;
    sq = getsq(&ring, 0);
    io_uring_prep_multishot_accept_direct(sq, fd, NULL, NULL, SOCK_NONBLOCK);
    char buf[64];
    struct iovec vec;
    vec.iov_base = buf;
    vec.iov_len = sizeof(buf);
    io_uring_register_buffers(&ring, &vec, 1);
    int t = time(NULL);
    unsigned c = 0, naccept = N, nsend = 0, nclose = 0;
    while (1) {
        err("submit", io_uring_submit(&ring));
        struct io_uring_cqe* cq;
        err("cq", io_uring_wait_cqe(&ring, &cq));
        unsigned long job = cq->user_data;
        int res = cq->res;
        unsigned flag = cq->flags;
        io_uring_cqe_seen(&ring, cq);
        switch (job) {
        case 0:
            if (!(flag & IORING_CQE_F_MORE)) {
                sq = getsq(&ring, 0);
                io_uring_prep_multishot_accept_direct(sq, fd, NULL, NULL, SOCK_NONBLOCK);
            }
            if (wrn("accept", res) >= 0) {
                naccept -= 1;
                nsend += 1;
                sq = getsq(&ring, 1);
                io_uring_prep_send(sq, res, MSG, sizeof(MSG), MSG_ZEROCOPY);
                sq->flags = IOSQE_IO_HARDLINK | IOSQE_FIXED_FILE;
                sq = getsq(&ring, 2);
                io_uring_prep_close_direct(sq, res);
            }
            break;
        case 1:
            if (wrn("send", res) > 0) c += 1;
            nsend -= 1;
            nclose += 1;
            break;
        default:
            wrn("close", res);
            nclose -= 1;
            naccept += 1;
        }
        if (time(NULL) - t >= 1) {
            printf("%u accept%u send%u close%u\n", c, naccept, nsend, nclose);
            t = time(NULL);
            c = 0;
        }
    }
}
