/* server-psk-nonblocking.c
 * A server ecample using a TCP connection with PSK security and non blocking. 
 *  
 * This file is part of CyaSSL.
 *
 * CyaSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * CyaSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <cyassl/ssl.h> /* include cyassl security */
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>      /* needed for runing nonblocking connections */
#include <time.h>       /* for time out on read loop */

#define MAXLINE     4096
#define LISTENQ     1024
#define SERV_PORT   11111

/* states of the tcp connection */
enum{
    TEST_SELECT_FAIL,
    TEST_TIMEOUT,
    TEST_RECV_READY,
    TEST_ERROR_READY
};

/* 
 * Fatal error detected, print out and exit. 
 */
void err_sys(const char *err, ...)
{
    printf("Fatal error : %s\n", err);
}

/* 
 * Handles response to client.
 */
int respond(CYASSL* ssl)
{
    time_t start_time;
    time_t current_time;
    int seconds = 10;
    int err;
    int  n;              /* length of string read */
    char buf[MAXLINE];   /* string read from client */
    char response[] = "I hear ya for shizzle";
    memset(buf, 0, MAXLINE);
    n = CyaSSL_read(ssl, buf, MAXLINE);
    time(&start_time);
    if (CyaSSL_write(ssl, response, strlen(response)) != strlen(response)) {
        err_sys("respond: write error");
        return 1;
    }

    /* timed loop to continue checking for a client message */
    do {
        if (n < 0) {
            
            err = CyaSSL_get_error(ssl, 0);
            if (err != SSL_ERROR_WANT_READ) {
                err_sys("respond: read error");
                return 1;
            }
            
            n = CyaSSL_read(ssl, buf, MAXLINE);
        }
        time(&current_time);
    } while (err == SSL_ERROR_WANT_READ && n < 0 && 
             difftime(current_time, start_time) < seconds);
    if (n > 0) {
        printf("%s\n", buf);
    } else {
       printf("Error: Timeout reached before read response");
       return 1;
    }
    
    return 0;
}

/*
 *Used for finding psk value.
 */
inline unsigned int my_psk_server_cb(CYASSL* ssl, const char* identity,
                                   unsigned char* key, unsigned int key_max_len)
{
    (void)ssl;
    (void)key_max_len;

    if (strncmp(identity, "Client_identity", 15) != 0)
        return 0;

    key[0] = 26;
    key[1] = 43;
    key[2] = 60;
    key[3] = 77;

    return 4;
}

/*
 *Pulled in from cyassl/test.h
 *Select the tcp, used when nonblocking. Checks the status of the connection.
 */
inline int tcp_select(int sockfd, int to_sec)
{
    fd_set recvfds, errfds;
    int nfds = sockfd + 1;
    struct timeval timeout = {to_sec, 0};
    int result;
    
    /* reset socket values */
    FD_ZERO(&recvfds);
    FD_SET(sockfd, &recvfds);
    FD_ZERO(&errfds);
    FD_SET(sockfd, &errfds);

    result = select(nfds, &recvfds, NULL, &errfds, &timeout);

    /* logic for which enumerated value is returned */
    if (result == 0)
        return TEST_TIMEOUT;
    else if (result > 0) {
        if (FD_ISSET(sockfd, &recvfds))
            return TEST_RECV_READY;
        else if (FD_ISSET(sockfd, &errfds))
            return TEST_ERROR_READY;
    }

    return TEST_SELECT_FAIL;
}

/*
 *Pulled in from examples/server/server.c
 *Function to handle nonblocking. Loops until tcp_select notifies that it's
 *ready for action. 
 */
void NonBlockingSSL(CYASSL* ssl)
{
    int ret;
    int error;
    int select_ret;
    int sockfd = (int)CyaSSL_get_fd(ssl);
    ret = CyaSSL_accept(ssl);
    error = CyaSSL_get_error(ssl, 0);
    while (ret != SSL_SUCCESS && (error == SSL_ERROR_WANT_READ ||
                                  error == SSL_ERROR_WANT_WRITE)) {
        int currTimeout = 1;

        /* print out for user notification */
        if (error == SSL_ERROR_WANT_READ)
            printf("... server would read block\n");
        else
            printf("... server would write block\n");

        select_ret = tcp_select(sockfd, currTimeout);
        
        /* if tcp_select signals ready try to accept otherwise continue loop*/
        if ((select_ret == TEST_RECV_READY) || 
            (select_ret == TEST_ERROR_READY)) {
            ret = CyaSSL_accept(ssl);
            error = CyaSSL_get_error(ssl, 0);
        }
        else if (select_ret == TEST_TIMEOUT && !CyaSSL_dtls(ssl)) {
            error = SSL_ERROR_WANT_READ;
        }
        else {
            error = SSL_FATAL_ERROR;
        }
    }
    /* faliure to accept */
    if (ret != SSL_SUCCESS)
        err_sys("SSL_accept failed");
}

int main()
{
    int                 listenfd, connfd;
    int                 opt;
    struct sockaddr_in  cliAddr, servAddr;
    char                buff[MAXLINE];
    socklen_t           cliLen;
    CyaSSL_Init();

    /* use psk suite for security */ 
    CyaSSL_CTX_set_psk_server_callback(ctx, my_psk_server_cb);
    CyaSSL_CTX_use_psk_identity_hint(ctx, "cyassl server");
    if (CyaSSL_CTX_set_cipher_list(ctx, "PSK-AES128-CBC-SHA256")
        != SSL_SUCCESS)
        err_sys("server can't set cipher list");

    /* find a socket */ 
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        err_sys("socket error");
    }

    /* set up server address and port */
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family      = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port        = htons(SERV_PORT);

    /* bind to a socket */
    opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&opt,
               sizeof(int));
    if (bind(listenfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0)
        err_sys("bind error");
        

    /* main loop for accepting and responding to clients */
    for ( ; ; ) {
        CYASSL* ssl;
        
        /* listen to the socket */   
        if (listen(listenfd, LISTENQ) < 0) {
            err_sys("listen error");
            return 1;
        }
        
        cliLen = sizeof(cliAddr);
        connfd = accept(listenfd, (struct sockaddr *) &cliAddr, &cliLen);
        if (connfd < 0) {
            if (errno != EINTR) {
                err_sys("accept error");
                return 1;   
            }
        }
        else {
            printf("Connection from %s, port %d\n",
                   inet_ntop(AF_INET, &cliAddr.sin_addr, buff, sizeof(buff)),
                   ntohs(cliAddr.sin_port));
    
            /* create CYASSL object */
            if ((ssl = CyaSSL_new(ctx)) == NULL) {
                err_sys("CyaSSL_new error");
                return 1;   
            }
            CyaSSL_set_fd(ssl, connfd);

            /* set CyaSSL and socket to non blocking and respond */
            CyaSSL_set_using_nonblock(ssl, 1);
            if (fcntl(connfd, F_SETFL, O_NONBLOCK) < 0) {
                err_sys("fcntl set failed");
                return 1;
            }
            NonBlockingSSL(ssl); 
            if (respond(ssl) != 0) {
                return 1;   
            }

            /* closes the connections after responding */
            CyaSSL_shutdown(ssl);
            CyaSSL_free(ssl);
            if (close(connfd) == -1) {
                err_sys("close error");
                return 1;
            }
        }
    }
    /* free up memory used by cyassl */
    CyaSSL_CTX_free(ctx);
    CyaSSL_Cleanup();
    
    return 0;
}

