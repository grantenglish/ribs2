/*
    This file is part of RIBS2.0 (Robust Infrastructure for Backend Systems).
    RIBS is an infrastructure for building great SaaS applications (but not
    limited to).

    Copyright (C) 2012 Adap.tv, Inc.

    RIBS is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, version 2.1 of the License.

    RIBS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with RIBS.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "ribs.h"
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <netdb.h>
#include <sys/eventfd.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/sendfile.h>

int _ribified_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int flags=fcntl(sockfd, F_GETFL);
    if (0 > fcntl(sockfd, F_SETFL, flags | O_NONBLOCK))
        return LOGGER_PERROR("fcntl"), -1;

    int res = connect(sockfd, addr, addrlen);
    if (res < 0 && errno != EINPROGRESS) {
        return res;
    }
    return ribs_epoll_add(sockfd, EPOLLIN | EPOLLOUT | EPOLLET, event_loop_ctx);
}

int _ribified_fcntl(int fd, int cmd, ...) {
    va_list ap;
    long arg;

    va_start (ap, cmd);
    arg = va_arg (ap, long);
    va_end (ap);

    if (F_SETFL == cmd)
        arg |= O_NONBLOCK;

    return fcntl(fd, cmd, arg);
}

ssize_t _ribified_read(int fd, void *buf, size_t count) {
    int res;

    epoll_worker_set_fd_ctx(fd, current_ctx);
    while ((res = read(fd, buf, count)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(fd, event_loop_ctx);
    return res;
}

ssize_t _ribified_write(int fd, const void *buf, size_t count) {
    int res;

    epoll_worker_set_fd_ctx(fd, current_ctx);
    while ((res = write(fd, buf, count)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(fd, event_loop_ctx);
    return res;
}

ssize_t _ribified_recvfrom(int sockfd, void *buf, size_t len, int flags,
                      struct sockaddr *src_addr, socklen_t *addrlen) {
    int res;

    epoll_worker_set_fd_ctx(sockfd, current_ctx);
    while ((res = recvfrom(sockfd, buf, len, flags, src_addr, addrlen)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(sockfd, event_loop_ctx);
    return res;
}

ssize_t _ribified_send(int sockfd, const void *buf, size_t len, int flags) {
    int res;

    epoll_worker_set_fd_ctx(sockfd, current_ctx);
    while ((res = send(sockfd, buf, len, flags)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(sockfd, event_loop_ctx);
    return res;
}

ssize_t _ribified_recv(int sockfd, void *buf, size_t len, int flags) {
    int res;

    epoll_worker_set_fd_ctx(sockfd, current_ctx);
    while ((res = recv(sockfd, buf, len, flags)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(sockfd, event_loop_ctx);
    return res;
}

ssize_t _ribified_readv(int fd, const struct iovec *iov, int iovcnt) {
    int res;

    epoll_worker_set_fd_ctx(fd, current_ctx);
    while ((res = readv(fd, iov, iovcnt)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(fd, event_loop_ctx);
    return res;
}

ssize_t _ribified_writev(int fd, const struct iovec *iov, int iovcnt) {
    int res;

    epoll_worker_set_fd_ctx(fd, current_ctx);
    while ((res = writev(fd, iov, iovcnt)) < 0) {
        if (errno != EAGAIN)
            break;
        yield();
    }
    epoll_worker_set_fd_ctx(fd, event_loop_ctx);
    return res;
}

ssize_t _ribified_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    ssize_t res;
    ssize_t t_res = 0;

    epoll_worker_set_fd_ctx(out_fd, current_ctx);
    for(;;yield()) {
        res = sendfile(out_fd, in_fd, offset, count);
        if (0 < res) {
            t_res += res;
            count -= res;
            if (0 == count)
                break;
            continue;
        }
        if (errno != EAGAIN) {
            t_res = res;
            break;
        }
    }
    epoll_worker_set_fd_ctx(out_fd, event_loop_ctx);
    return t_res;
}

int _ribified_pipe2(int pipefd[2], int flags) {

    if (0 > pipe2(pipefd, flags | O_NONBLOCK))
        return -1;

    if (0 == ribs_epoll_add(pipefd[0], EPOLLIN | EPOLLET, event_loop_ctx) &&
        0 == ribs_epoll_add(pipefd[1], EPOLLOUT | EPOLLET, event_loop_ctx))
        return 0;

    int my_error = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    errno = my_error;
    return -1;
}

int _ribified_pipe(int pipefd[2]) {
    return _ribified_pipe2(pipefd, 0);
}

int _ribified_nanosleep(const struct timespec *req, struct timespec *rem) {
    int tfd = ribs_sleep_init();
    if (0 > tfd) return -1;
    int res = ribs_nanosleep(tfd, req, rem);
    close(tfd);
    return res;
}

unsigned int _ribified_sleep(unsigned int seconds) {
    struct timespec req = {seconds, 0};
    _ribified_nanosleep(&req, NULL);
    return 0;
}

int _ribified_usleep(useconds_t usec) {
    struct timespec req = {usec/1000000L, (usec%1000000L)*1000L};
    return _ribified_nanosleep(&req, NULL);
}

void *_ribified_malloc(size_t size) {
    if (0 == size) return NULL;
    ++current_ctx->ribify_memalloc_refcount;
    void *mem = memalloc_alloc(&current_ctx->memalloc, size + sizeof(uint32_t));
    *(uint32_t *)mem = size;
    return mem + sizeof(uint32_t);
}

void _ribified_free(void *ptr) {
    if (NULL == ptr) return;
    --current_ctx->ribify_memalloc_refcount;
}

void *_ribified_calloc(size_t nmemb, size_t size) {
    size_t s = nmemb * size;
    void *mem = _ribified_malloc(s);
    memset(mem, 0, s);
    return mem;
}

void *_ribified_realloc(void *ptr, size_t size) {
    if (NULL == ptr) return _ribified_malloc(size);
    if (memalloc_is_mine(&current_ctx->memalloc, ptr))
        --current_ctx->ribify_memalloc_refcount; // consider mine as freed
    size_t old_size = *(uint32_t *)(ptr - sizeof(uint32_t));
    void *mem = _ribified_malloc(size);
    memcpy(mem, ptr, size > old_size ? old_size : size);
    return mem;
}


#ifdef UGLY_GETADDRINFO_WORKAROUND
int _ribified_getaddrinfo(const char *node, const char *service,
                     const struct addrinfo *hints,
                     struct addrinfo **results) {

    struct gaicb cb = { .ar_name = node, .ar_service = service, .ar_request = hints, .ar_result = NULL };
    struct gaicb *cb_p[1] = { &cb };

    struct sigevent sevp;
    sevp.sigev_notify = SIGEV_SIGNAL;
    sevp.sigev_signo = SIGRTMIN; /* special support in epoll_worker.c */
    sevp.sigev_value.sival_ptr = current_ctx;
    sevp.sigev_notify_attributes = NULL;

    int res = getaddrinfo_a(GAI_NOWAIT, cb_p, 1, &sevp);
    if (!res) {
        yield();
        res = gai_error(cb_p[0]);
        *results = cb.ar_result;
    }
    return res;
}
#endif
