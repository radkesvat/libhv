#include "hloop.h"
#include "hsocket.h"

#include <stdio.h>
#include <stdlib.h>

static int heartbeats;

static void heartbeat(hio_t* io) {
    (void)io;
    ++heartbeats;
}

static void require(int ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "detach_timer_test: %s\n", message);
        exit(1);
    }
}

static void wait_for_heartbeat(hloop_t* loop) {
    int before = heartbeats;
    for (int i = 0; i < 8 && heartbeats == before; ++i) {
        require(hloop_run(loop) == 0, "run heartbeat loop");
    }
    require(heartbeats == before + 1, "armed heartbeat must run on its owner loop");
}

int main(void) {
    hloop_t* source = hloop_new(HLOOP_FLAG_RUN_ONCE);
    hloop_t* target = hloop_new(HLOOP_FLAG_RUN_ONCE);
    hio_t* io = hloop_create_udp_server(source, "127.0.0.1", 0);
    require(io != NULL, "create socket");
    int fd = hio_fd(io);

    require(hio_read(io) == 0, "start read interest");
    require(hio_try_detach(io) < 0 && hio_exists(source, fd), "active read must stay on source");
    require(hio_read_stop(io) == 0, "stop read interest");
    hio_set_read_timeout(io, 60000);
    require(hio_try_detach(io) < 0 && hio_exists(source, fd), "read timeout must keep socket on source");
    hio_set_read_timeout(io, 0);

    hio_set_heartbeat(io, 1, heartbeat);
    require(hio_try_detach(io) < 0 && hio_exists(source, fd), "heartbeat must keep socket on source");
    wait_for_heartbeat(source);
    hio_set_heartbeat(io, 0, NULL);

#if WITH_KCP
    require(hio_set_kcp(io, NULL) == 0, "enable KCP");
#endif
    require(hio_try_detach(io) == 0 && !hio_exists(source, fd), "detach after disarming timers");
    require(hio_try_detach(io) < 0, "a detached socket cannot be detached twice");
    hio_attach(target, io);
    require(hio_exists(target, fd) && hevent_loop(io) == target, "attach to new owner");
    hio_set_heartbeat(io, 1, heartbeat);
    wait_for_heartbeat(target);
    hio_set_heartbeat(io, 0, NULL);

#if WITH_KCP
    uint32_t idle_timer_count = hloop_ntimers(target);
    hio_set_peeraddr(io, hio_localaddr(io), sizeof(struct sockaddr_in));
    require(hio_write(io, "K", 1) == 1, "start KCP peer");
    require(hio_try_detach(io) < 0 && hio_exists(target, fd), "KCP update timer must keep socket on destination");
    require(hio_close_rudp(io, NULL) == 0, "close the KCP peer");
    for (int i = 0; i < 8 && hloop_ntimers(target) > idle_timer_count; ++i) {
        require(hloop_run(target) == 0, "retire the KCP peer");
    }
    require(hloop_ntimers(target) == idle_timer_count, "KCP update timer must retire");
#endif
    hio_detach(io);
    hio_attach(source, io);
    require(hio_exists(source, fd), "socket without timers must be transferable again");
    hloop_free(&target);
    hloop_free(&source);
    return 0;
}
