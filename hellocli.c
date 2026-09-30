#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
const char MSG[] = "GET / HTTP/1.1\r\n\r\n";
const int N = 2048;

static int wrn(const char* s, int res) {
    if (res < 0) fprintf(stderr, "%s %s\n", s, strerror(-res));
    return res;
}

static int err(const char* s, int res) {
    if (wrn(s, res) < 0) exit(1);
    return res;
}

static struct io_uring_sqe* getsq(struct io_uring* ring, unsigned char job) {
    struct io_uring_sqe* sq = io_uring_get_sqe(ring);
    if (!sq) err("sq", -1);
    sq->user_data = job;
    return sq;
}

int main(int argc, char** argv) {
    struct sockaddr_in adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin_family = AF_INET;
    adr.sin_port = htons(8080);
    err("pton", inet_pton(AF_INET, "127.0.0.1", &adr.sin_addr) != 1);
    struct io_uring ring;
    struct io_uring_params prm;
    memset(&prm, 0, sizeof(prm));
    prm.flags = IORING_SETUP_SQPOLL;
    prm.sq_thread_idle = 20;
    err("init", io_uring_queue_init_params(N * 4, &ring, &prm));
    struct io_uring_sqe* sq;
    for (int i = 0; i < N; ++i) {
        sq = getsq(&ring, 0);
        io_uring_prep_socket(sq, AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0, 0);
    }
    char buf[64];
    struct iovec vec;
    vec.iov_base = buf;
    vec.iov_len = sizeof(buf);
    io_uring_register_buffers(&ring, &vec, 1);
    int t = time(NULL);
    unsigned c = 0, nsock = N, ncon = 0, nrecv = 0, nclose = 0;
    while (1) {
        err("submit", io_uring_submit(&ring));
        struct io_uring_cqe* cq;
        err("cq", io_uring_wait_cqe(&ring, &cq));
        unsigned char job = cq->user_data;
        int res = cq->res;
        io_uring_cqe_seen(&ring, cq);
        switch (job) {
        case 0:
            if (wrn("socket", res) >= 0) {
                nsock -= 1;
                ncon += 1;
                sq = getsq(&ring, 1);
                io_uring_prep_connect(sq, res, (struct sockaddr*)&adr, sizeof(adr));
                sq->flags = IOSQE_IO_LINK;
                sq = getsq(&ring, 2);
                io_uring_prep_readv(sq, res, &vec, 1, 0);
                sq->flags = IOSQE_IO_HARDLINK;
                sq = getsq(&ring, 3);
                io_uring_prep_close(sq, res);
                sq->flags = IOSQE_IO_HARDLINK;
            }
            sq = getsq(&ring, 0);
            io_uring_prep_socket(sq, AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0, 0);
            break;
        case 1:
            wrn("connect", res);
            ncon -= 1;
            nrecv += 1;
            break;
        case 2:
            if (wrn("recv", res - 1) > 0) c += 1;
            nrecv -= 1;
            nclose += 1;
            break;
        default:
            wrn("close", res);
            nclose -= 1;
            nsock += 1;
        }
        if (time(NULL) - t >= 1) {
            printf("%u nsock%u ncon%u nrecv%u nclose%u\n", c, nsock, ncon, nrecv, nclose);
            t = time(NULL);
            c = 0;
        }
    }
}
