#include "hloop.h"
#include "hsocket.h"

static void require(int ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "write_return_test: %s\n", message);
        exit(1);
    }
}

static void close_on_write(hio_t* io, const void* buf, int len) {
    (void)buf;
    require(len == 1 && hio_close(io) == 0, "could not close the socket in the write callback");
}

#ifdef OS_UNIX
static int replacement_fd;

static void replace_on_write(hio_t* io, const void* buf, int len) {
    hloop_t* loop = hevent_loop(io);
    int fd = hio_fd(io);
    uint32_t id = hio_id(io);
    close_on_write(io, buf, len);
    require(dup2(replacement_fd, fd) == fd, "could not reuse the descriptor");
    require(hio_id(hio_get(loop, fd)) != id, "the replacement socket kept the old ID");
}
#endif

static void check_write(hloop_t* loop, hwrite_cb cb) {
    int pair[2];
    require(Socketpair(AF_INET, SOCK_STREAM, 0, pair) == 0, "could not open a socket pair");
    require(so_rcvtimeo(pair[1], 1000) == 0, "could not set the peer's receive timeout");
    hio_t* io = hio_get(loop, pair[0]);
    require(hwrite(loop, pair[0], "a", 1, NULL) == io, "a normal write lost its open handle");
    require(hwrite(loop, pair[0], "b", 1, cb) == NULL, "write returned a handle after its callback retired the socket");
    char byte;
    require(recv(pair[1], &byte, 1, 0) == 1 && byte == 'a', "the peer missed the first write");
    require(recv(pair[1], &byte, 1, 0) == 1 && byte == 'b', "the peer missed the write that closed the socket");
    closesocket(pair[1]);
}

int main(void) {
    hloop_t* loop = hloop_new(0);
#ifdef OS_UNIX
    int replacement[2];
    require(Socketpair(AF_INET, SOCK_STREAM, 0, replacement) == 0, "could not open the replacement socket pair");
    replacement_fd = replacement[0];
    check_write(loop, replace_on_write);
    closesocket(replacement[0]);
    closesocket(replacement[1]);
#endif
    check_write(loop, close_on_write);
    hloop_free(&loop);
    return 0;
}
