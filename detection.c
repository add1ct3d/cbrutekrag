/*
Copyright (c) 2014-2018 Jorge Matricali

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#include "log.h"
#include "cbrutekrag.h"
#include "wordlist.h"
#include "progressbar.h"

#define BUF_SIZE 1024

int scan_counter = 0;
wordlist_t filtered;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int detection_detect_ssh(char *serverAddr, unsigned int serverPort, unsigned int tm)
{
    struct sockaddr_in addr;
    int sockfd, ret;
    char buffer[BUF_SIZE];
    char *banner = NULL;
    fd_set fdset;

    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        log_error("Error creating socket!");
        sockfd = 0;
        return -1;
    }
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverPort);
    addr.sin_addr.s_addr = inet_addr(serverAddr);

    log_debug("[-] %s:%d - Connecting...", serverAddr, serverPort);
    ret = connect(sockfd, (struct sockaddr *) &addr, sizeof(addr));

    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);

    /* Connection timeout */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1) {
        int so_error;
        socklen_t len = sizeof so_error;

        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            log_debug("%s:%d - Error connecting to the server! (%s)", serverAddr, serverPort, strerror(so_error));
            close(sockfd);
            sockfd = 0;
            return -1;
        }
    } else {
        close(sockfd);
        sockfd = 0;
        return -1;
    }

    // Set to blocking mode again...
    if ((ret = fcntl(sockfd, F_GETFL, NULL)) < 0) {
        log_error("Error fcntl(..., F_GETFL) (%s)\n", strerror(ret));
        close(sockfd);
        sockfd = 0;
        return -2;
    }

    long arg = 0;
    arg &= (~O_NONBLOCK);

    if ((ret = fcntl(sockfd, F_SETFL, arg)) < 0) {
       log_error("Error fcntl(..., F_SETFL) (%s)\n", strerror(ret));
       close(sockfd);
       sockfd = 0;
       return -1;
    }

    log_debug("[+] %s:%d - Connected.", serverAddr, serverPort);

    /* Send/Receive timeout */
    struct timeval timeout;
    timeout.tv_sec = tm;
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                sizeof(timeout));

    memset(buffer, 0, BUF_SIZE);

    // RECIBIR BANNER
    ret = recvfrom(sockfd, buffer, BUF_SIZE, 0, NULL, NULL);
    if (ret < 0) {
        log_error("%s:%d - Error receiving banner!", serverAddr, serverPort);
        close(sockfd);
        sockfd = 0;
        return -1;
    }
    banner = malloc(sizeof(char) * 1024);
    banner = strdup(strtok(buffer, "\n"));
    log_debug("[?] %s:%d - %s", serverAddr, serverPort, banner);

    char *pkt1 = "SSH-2.0-OpenSSH_7.5";
    char *pkt2 = "\n";
    char *pkt3 = "asd\n      ";
    char *search = "Protocol mismatch.";

    log_debug("[<] %s:%d - Sending pkt1: %s", serverAddr, serverPort, strtok(pkt1, "\n"));
    ret = sendto(sockfd, pkt1, sizeof(pkt1), 0, (struct sockaddr *) &addr, sizeof(addr));

    if (ret < 0) {
        log_error("%s:%d - Error sending data pkt1!!", serverAddr, serverPort);
        close(sockfd);
        sockfd = 0;
        return -1;
    }

    log_debug("[<] %s:%d - Sending pkt2: %s", serverAddr, serverPort, pkt2);
    ret = sendto(sockfd, pkt2, sizeof(pkt2), 0, (struct sockaddr *) &addr, sizeof(addr));

    if (ret < 0) {
        log_error("%s:%d - Error sending data pkt2!!", serverAddr, serverPort);
        close(sockfd);
        sockfd = 0;
        return -1;
    }

    log_debug("[<] %s:%d - Sending pkt3: %s", serverAddr, serverPort, pkt3);
    ret = sendto(sockfd, pkt3, sizeof(pkt3), 0, (struct sockaddr *) &addr, sizeof(addr));

    if (ret < 0) {
        log_error("%s:%d - Error sending data pkt3!!", serverAddr, serverPort);
        close(sockfd);
        sockfd = 0;
        return -1;
    }

    log_debug("[>] %s:%d - Receiving...", serverAddr, serverPort);
    ret = recvfrom(sockfd, buffer, BUF_SIZE, 0, NULL, NULL);
    if (ret < 0) {
        log_error("%s:%d - Error receiving response!!", serverAddr, serverPort);
        close(sockfd);
        sockfd = 0;
        return -1;
    }

    close(sockfd);
    sockfd = 0;

    log_debug("[+] %s:%d - Received: %s", serverAddr, serverPort, buffer);

    if (strstr(buffer, search) != NULL) {
        log_debug("[+] %s:%d - %s\n", serverAddr, serverPort, banner);
        return 0;
    }

    log_error("[!] %s:%d - POSSIBLE HONEYPOT!\n", serverAddr, serverPort);
    return 1;
}

void *detection_process(void *ptr)
{
    wordlist_t *targets = (wordlist_t *) ptr;
    while (scan_counter < targets->length - 1) {
        pthread_mutex_lock(&mutex);
        scan_counter++;
        if (! g_verbose) {
            char str[36];
            snprintf(str, 36, "[%d/%zu] %zu OK - %s:22", scan_counter, targets->length, filtered.length, targets->words[scan_counter-1]);
            progressbar_render(scan_counter, targets->length, str, -1);
        }
        pthread_mutex_unlock(&mutex);

        if (detection_detect_ssh(targets->words[scan_counter-1], 22, 1) == 0) {
            pthread_mutex_lock(&mutex);
            wordlist_append(&filtered, targets->words[scan_counter-1]);
            pthread_mutex_unlock(&mutex);
        }
    }
    pthread_exit(NULL);
    return NULL;
}

void detection_start(wordlist_t *source, wordlist_t *target, int max_threads)
{
    filtered.length = 0;
    filtered.words = NULL;

    pthread_t scan_threads[max_threads];
    int ret;

    for (int i = 0; i < max_threads; i++) {
        if ((ret = pthread_create(&scan_threads[i], NULL, &detection_process, (void *) source))) {
            log_error("Thread creation failed: %d\n", ret);
        }
    }

    for (int i = 0; i < max_threads; i++) {
        if (scan_threads[i] != NULL) {
            pthread_join(scan_threads[i], NULL);
        }
    }

    *target = filtered;
}
