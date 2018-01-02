/* 
 * File:   main.cpp
 * Author: Ivan
 * !!SERVER!!
 * Here is a main file for network application. It uses znet lib: https://github.com/starwing/znet
 * A -liphlp  and -lws2_32 linker options may help
 */
/* tell znet include all implement details into here */ 
#define ZN_IMPLEMENTATION
/* include znet library: only need a header file! */
#include "znet.h"

#include <cstdlib>
#include <stdio.h>
/* we make a macro to compute a literal string's length, and put
 * length after string. */
#define send_string(str) str, sizeof(str)-1
/* znet doesn't maintain send/receive buffers. we should maintain them
 * ourselves. these buffer should alive in a whole receive duration
 * (from zn_recv() to on_recv() callback), or a whole send duration
 * (from zn_send() to on_send() callback). So we should allocate them
 * on heap. we make a struct for hold buffer and other useful
 * informations. we could set this data as the last argument (ud) of
 * zn_recv()/zn_send(), and retrieve them from the first pointer of
 * callbacks.  */
#define MYDATA_BUFLEN 1024

using namespace std;

typedef struct MyData {
    char buffer[MYDATA_BUFLEN];
    int idx;
    int count;
} MyData;
    zn_State  *S;         /* the znet event loop handler. keep it global */
    zn_Tcp    *tcp;       /* znet tcp client handler     */
/* function to accept a new coming connection. */
void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp);
/* function when a tcp in server mode received something. */
void on_server_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);
/* function when a tcp in server mode sent something. */
void on_server_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

/* functions that will called after some milliseconds after.  */
zn_Time on_timer(void *ud, zn_Timer *timer, zn_Time elapsed);

static void cleanup(zn_State *in_S);

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


int main(int argc, char** argv) {

    zn_Timer  *timer;     /* znet timer handler          */
    zn_Accept *accept;    /* znet tcp service handler    */
    /* we can print out which engine znet uses: */
    printf("znet example: use %s engine.\n", zn_engine());    
    /* first, we initialize znet global environment. we needn't do
     * this on *nix or Mac, because on Windows we should initialize
     * and free the WinSocks2 global service, the
     * zn_initialize()/zn_deinitialize() function is prepared for this
     * situation. */
    zn_initialize();
    /* after network environment is ready, we can create a new event
     * loop handler now. the zn_State object is the center object that
     * hold any resource znet uses, when everything overs, you should
     * call zn_close() to clean up all resources znet used. */
    S = zn_newstate();
    if (S == NULL) {
        fprintf(stderr, "create znet handler failured\n");
        return 2; /* error out */
    }
    /* create a znet tcp server */
    accept = zn_newaccept(S);
        /* this server listen to 8080 port */
    if (zn_listen(accept, "127.0.0.1", 9999) == ZN_OK) {
        printf("[%p] accept listening to 9999 ...\n", accept);
    }
    
    /* this server and when new connection coming, on_accept()
     * function will be called.
     * the 3rd argument of zn_accept will be send to on_accept as-is.
     * we don't use this pointer here, but will use in when send
     * messages. (all functions that required a callback function
     * pointer all have this user-data pointer */
    zn_accept(accept, on_accept, NULL);
    printf("zn_accept called\n");
    timer = zn_newtimer(S, on_timer, accept);
    zn_starttimer(timer, 30000);
    //run console handler
    register_interrupted();
    
        /* now all prepare work are done. we now run poller to process all
     * subsequent messages. */
    zn_run(S, ZN_RUN_LOOP);

    /* when server dowm (all zn_Accept object are deleted), zn_run()
     * will return, we can cleanup resources in zn_State object now */
    //zn_close(S);

    /* and shutdown global environment */
    //zn_deinitialize();
    
    return 0;
}

/* we stop server after 3 seconds. */
zn_Time on_timer(void *ud, zn_Timer *timer, zn_Time elapsed) {
    zn_Accept *accept = (zn_Accept*)ud;
    printf("close accept(%p) after 3s\nexit...\n", accept);
    zn_delaccept(accept);
    /* return value is the next time the timer callback function
     * called, return 0 means we want delete this timer and don't
     * called again. */
    printf("call before return in on_timer");
    //the time finished, free resources
    
    //free(data);
    return 0;
}

/* the server accept callback: when a new connection comes, this
 * function will be called. you must call zn_accept() to notify znet
 * you are done with this connection and ready to accept another
 * connection.  */
void on_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    /* if err is not ZN_OK, we meet errors. simple return. */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] some bad thing happens to server\n"
                "  when accept connection: %sn", accept, zn_strerror(err));
        return;
    }

    printf("[%p] a new connection(%p) is comming :-)\n", accept, tcp);

    /* Now we sure a real connection comes. when a connection comes,
     * we receive messages from connection.
     * first, we receive something from client, so we need a buffer to
     * hold result: */
    MyData *data = (MyData*)malloc(sizeof(MyData));
    /* OK, send recv request to znet, and when receive done, our
     * on_server_recv() function will be called, with our data
     * pointer. */
    zn_recv(tcp, data->buffer, MYDATA_BUFLEN, on_server_recv, data);

    /* at the same time, we send some greet message to our guest: */
    zn_send(tcp, send_string("welcome to connect our server :)\n"), on_server_sent, NULL);

    /* now we done with this connection, all subsequent operations
     * will be done in this connection, but not here. we are ready to
     * accept another connection now. */
    zn_accept(accept, on_accept, NULL);
}

void on_server_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    /* send work may error out, we first check the result code: */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] server meed problems when send something: %s\n",
                tcp, zn_strerror(err));
        return;
    }

    /* now we know that the message are sent to the server, we can
     * send more messages here.
     * Notice that when we send our first greet message, the ud
     * pointer we send is NULL, so we could use this to determine
     * the times we done the send work.  */

    if (ud == NULL) { /* the first time? */
        printf("[%p] first send to client done\n", tcp);
        zn_send(tcp, send_string("this is our second message."),
                on_server_sent, (void*)1);
    }
    else { /* not the first time? */
        /* do nothing. but a log. */
        printf("[%p] second send to client done\n", tcp);
    }
}

/* when our connection receive something, znet will call this
 * function, but just when we call zn_recv() to tell znet "I'm ready
 * to process in-coming messages!", if you don't want accept messages
 * from client sometime, just do not call zn_recv(), and messages will
 * stay in your OS's buffer. */
void on_server_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    MyData *data = (MyData*)ud; /* our data from zn_recv() */

    /* we expect client close our link by itself. so if a err occurs,
     * maybe it means a connection is over, so we could delete it
     * safely. */
    if (err != ZN_OK) {
        fprintf(stderr, "[%p] error when receiving from client: %s\n",
                tcp, zn_strerror(err));
        /* after error, tcp object will be closed. but you still could
         * use it to do other things, e.g. to connect to other server.
         * but we don't wanna do that now, so put it back to znet.
         * you can also delete it in on_server_sent(), but in here we
         * could free our data.  */
        zn_deltcp(tcp);
        free(data); /* don't forget to free our buffer. */
        return;
    }

    printf("[%p] server receive something from client:"
            " %.*s (%d bytes)\n", tcp, (int)count, data->buffer, (int)count);

    /* make another receive process... */
    err = zn_recv(tcp, data->buffer, MYDATA_BUFLEN, on_server_recv, data);
    if (err != ZN_OK) {
        printf("[%p] prepare to receive error: %s\n", tcp, zn_strerror(err));
        zn_deltcp(tcp);
        free(data);
    }
}

static void cleanup(zn_State *in_S) {
    printf("exiting ... ");
    zn_close(in_S);
    printf("OK\n");
    printf("deinitialize ... ");
    zn_deinitialize();
    printf("OK\n");
}