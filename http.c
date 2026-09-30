#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#define PORT 8080
#define BUFSZ 4096

int ZCOPY = 1;
char *ZERO;

inline int sendexact(int fd, char *buf, int n, int flag) {
  int t;
  while (n && (t = send(fd, buf, n, flag))) {
    buf += t;
    n -= t;
  }
  return n;
}
inline int spliceexact(int fdin, int fdout, int n) {
  int t;
  while (n && (t = splice(fdin, NULL, fdout, NULL, n, SPLICE_F_MOVE))) {
    n -= t;
  }
  return n;
}
void handle_request(int fd) {
  char buf[BUFSZ];
  ssize_t n = read(fd, buf, BUFSZ - 1);
  if (n <= 0)
    return;

  buf[n] = '\0';
  char *cmd_start = strchr(buf, '/');
  if (!cmd_start)
    return;

  cmd_start++;
  char *cmd_end = strchr(cmd_start, ' ');
  if (!cmd_end)
    return;

  *cmd_end = '\0';

  // URL decode basic: %20 -> space
  for (char *p = cmd_start; *p; p++) {
    if (p[0] == '%' && p[1] && p[2]) {
      int hex;
      if (sscanf(p + 1, "%2x", &hex) == 1) {
        *p = (char)hex;
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
      }
    }
  }
  fprintf(stderr, "%s\n", cmd_start);

  int pipefd[2];
  if (pipe(pipefd) < 0)
    return;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd_start, NULL);
    exit(1);
  }
  close(pipefd[1]);

  char *httphead = "HTTP/1.0 200 OK\r\nTransfer-Encoding:chunked\r\n\r\n";
  sendexact(fd, httphead, strlen(httphead), 0);
  char head[8];
  int bufsz, headsz;
  if (ZCOPY) {
    while (!sendexact(fd, head, snprintf(head, sizeof(head), "%x\r\n", BUFSZ), 0) && !sendexact(fd, ZERO, bufsz = spliceexact(pipefd[0], fd, BUFSZ), MSG_ZEROCOPY) && !sendexact(fd, "\r\n", 2, 0) && !bufsz)
      ;
  } else {
    while ((bufsz = read(pipefd[0], buf, BUFSZ)) && !sendexact(fd, head, snprintf(head, sizeof(head), "%x\r\n", bufsz), 0) && !sendexact(fd, buf, bufsz, 0) && !sendexact(fd, "\r\n", 2, 0))
      ;
  }
  sendexact(fd, "0\r\n\r\n", 5, 0);

  close(pipefd[0]);
  close(fd);
}

int main() {
  signal(SIGCHLD, SIG_IGN);

  // Read environment variables
  char *env;
  if (env = getenv("ZCOPY"))
    ZCOPY = atoi(env);
  ZERO = malloc(BUFSZ);
  memset(ZERO, 0, BUFSZ);

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = INADDR_ANY};

  bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
  listen(server_fd, 5);

  printf("Listening on port %d\n", PORT);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

    if (fd >= 0) {
      pid_t pid = fork();
      if (pid == 0) {
        close(server_fd);
        handle_request(fd);
        exit(0);
      }
      close(fd);
    }
  }

  return 0;
  //???
}