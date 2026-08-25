// jan 21: you need to give the binary for this file cap_sys_nice capacity if u want to use the automation
#define _GNU_SOURCE
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TARGET_IP   "100.100.100.100" // Teensy server
#define TARGET_PORT 80
#define HTTP_PORT   8080

// Elevate to real-time priority
void set_realtime_priority() {
    struct sched_param param;
    param.sched_priority = 80; // range 1–99
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        perror("sched_setscheduler");
    } else {
        printf("Realtime priority set.\n");
    }
}

// Get current time in microseconds
static inline uint32_t now_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    return (uint32_t)(us & 0xFFFFFFFF);
}

//get ns
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); //use clock_monotonic instead of raw here since lttng and everyone else do that too
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// TCP client thread: talks to Teensy
void *tcp_client_thread(void *arg) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return NULL;
    }
    
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));  // <-- add here

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TARGET_PORT);
    inet_pton(AF_INET, TARGET_IP, &server_addr.sin_addr);


    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(0); // any local port
    inet_pton(AF_INET, "100.100.100.169", &local_addr.sin_addr); // <-- IP of desired NIC

    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        perror("bind(source ip)");
    }
    
    struct sockaddr_in local;
    socklen_t len = sizeof(local);

    if (getsockname(sockfd, (struct sockaddr *)&local, &len) == 0) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
        printf("Bound local IP: %s\n", ip);
        printf("Bound local port: %u\n", ntohs(local.sin_port));
    } else {
        perror("getsockname");
    }

    printf("Connecting to %s:%d...\n", TARGET_IP, TARGET_PORT);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return NULL;
    }
    printf("TCP connection established.\n");

    // Send time provider message
    const char *msg = "time provider\n";
    send(sockfd, msg, strlen(msg), 0);


    printf("Sent 'time provider'\n");

    uint8_t buf[8];
    while (1) {
        ssize_t n = recv(sockfd, buf, 8, MSG_WAITALL);
        if (n == 0) {
            printf("Connection closed by Teensy.\n");
            break;
        } else if (n < 0) {
            perror("recv");
            break;
        } else if (n < 8) {
            printf("Partial timestamp (%zd bytes), skipping\n", n);
            continue;
        }

        uint64_t original;
        memcpy(&original, buf, 8);

        uint64_t local = now_ns();
        uint8_t reply[16];
        memcpy(reply, buf, 8);
        memcpy(reply + 8, &local, 8);

        send(sockfd, reply, 16, 0);

        printf("Got 8 bytes: %02X %02X %02X %02X (%lu)\n",
               buf[0], buf[1], buf[2], buf[3], original);
        printf("Responded with local time: %lu\n", local);
    }

    close(sockfd);
    return NULL;
}

// Very simple HTTP server
void run_http_server() {
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTP_PORT);

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("HTTP server running on port %d\n", HTTP_PORT);

    char buffer[1024];
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "HTTP server is running.\n";

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) continue;
        read(client_fd, buffer, sizeof(buffer) - 1);
        write(client_fd, response, strlen(response));
        close(client_fd);
    }
}

int main() {
    set_realtime_priority();

    pthread_t tid;
    pthread_create(&tid, NULL, tcp_client_thread, NULL);
    pthread_detach(tid);

    run_http_server();
    return 0;
}

