/* 
 * File:   main.cpp
 * Author: Ivan
 * !!SERVER!!
 * Here is a main file for network application. It uses znet lib: https://github.com/starwing/znet
 * A -liphlp  and -lws2_32 linker options may help
 */
/* tell znet include all implement details into here */ 
#define ZN_IMPLEMENTATION
#include "znet.h"
#include "zn_buffer.h"
#include "zn_bufferpool.h"

#include <stdio.h>
#include <string.h>


char     addr[ZN_MAX_ADDRLEN] = "127.0.0.1";
unsigned port = 8081;

zn_State *S;
zn_BufferPool pool;
//server has sent something. Perform this callback after sending
static void on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        zn_deltcp(tcp);
        zn_putbuffer(&pool, data);
        return;
    }
    if (zn_sendfinish(&data->send, count))
        zn_send(tcp, zn_sendbuff(&data->send), zn_sendsize(&data->send),
                on_send, ud);
}
//
static size_t on_header(void *ud, const char *buff, size_t len) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    printf("client(%p) send: %.*s\n", (void*)data->tcp, (int)len, buff);
    if (zn_sendprepare(&data->send, buff, len)
            && zn_send(data->tcp,
                zn_sendbuff(&data->send),
                zn_sendsize(&data->send), on_send, ud) != ZN_OK)
    {
        zn_deltcp(data->tcp);
        zn_putbuffer(&pool, data);
    }
    return len;
}
//callback for subroutine to be used when server recieves some message
static void on_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    zn_BufferPoolNode *data = (zn_BufferPoolNode*)ud;
    if (err != ZN_OK) {
        zn_deltcp(tcp);
        zn_putbuffer(&pool, data);
        return;
    }
    zn_recvfinish(&data->recv, count);
    zn_recv(tcp, zn_recvbuff(&data->recv), zn_recvsize(&data->recv),
            on_recv, ud);
}
//callback for subroutine to be used when client connects
static void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    zn_BufferPoolNode *data = zn_getbuffer(&pool);
    if (err != ZN_OK)
        return;
    printf("client connected: %p\n", (void*)tcp);
    zn_recvonheader(&data->recv, on_header, data);
    data->tcp = tcp;
    if (zn_recv(tcp, zn_recvbuff(&data->recv), zn_recvsize(&data->recv),
                on_recv, data) != ZN_OK)
        zn_deltcp(tcp);
    zn_accept(accept, on_accept, ud);
}
//deinit and free all the resources
static void cleanup(void) {
    printf("exiting ... ");
    zn_close(S);
    printf("OK\n");
    printf("deinitialize ... ");
    zn_deinitialize();
    printf("OK\n");
}
//this code is required to handle CTRL-C interrupts in console app in a propper way
#ifdef _WIN32
static int deinited = 0;
static BOOL WINAPI on_interrupted(DWORD dwCtrlEvent) {
    if (!deinited) {
        deinited = 1;
        /* windows ctrl handler is running at another thread */
        zn_post(S, (zn_PostHandler*)cleanup, NULL);
    }
    return TRUE;
}

static void register_interrupted(void) {
    SetConsoleCtrlHandler(on_interrupted, TRUE);
}
#else
#include <signal.h>

static void on_interrupted(int signum) {
    if (signum == SIGINT)
        cleanup();
}

static void register_interrupted(void) {
   struct sigaction act; 
   act.sa_flags = SA_RESETHAND;
   act.sa_handler = on_interrupted;
   sigaction(SIGINT, &act, NULL);
}
#endif

int main(int argc, const char **argv) {
    

    zn_initialize();
    if ((S = zn_newstate()) == NULL) return 2;

    zn_initbuffpool(&pool);

    zn_Accept *accept;
    if ((accept = zn_newaccept(S)) == NULL) return 2;
    zn_listen(accept, addr, port);
    zn_accept(accept, on_accept, NULL);
    printf("listening at %s:%d ...\n", addr, port);

    register_interrupted();
    return zn_run(S, ZN_RUN_LOOP);
}