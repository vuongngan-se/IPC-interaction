/*
 * IPC - Server Program (TCP Socket) - Linux Ubuntu
 * Compile: gcc server.c -o server
 * Run:     ./server
 *
 * Server lắng nghe trên port 8080, chấp nhận kết nối từ client,
 * và trao đổi tin nhắn tương tác cho đến khi một bên gửi "quit".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT    8080
#define BUFSIZE 1024

int main(void)
{
    int    server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char   buffer[BUFSIZE];
    int    bytes;
    int    opt = 1;

    /* ---------- Tao socket ---------- */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Server] Socket creation failed");
        return 1;
    }

    /* Cho phep tai su dung port ngay sau khi dong */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[Server] setsockopt failed");
        close(server_fd);
        return 1;
    }

    /* ---------- Bind ---------- */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Server] Bind failed");
        close(server_fd);
        return 1;
    }

    /* ---------- Listen ---------- */
    if (listen(server_fd, 1) < 0) {
        perror("[Server] Listen failed");
        close(server_fd);
        return 1;
    }
    printf("[Server] Listening on port %d ...\n", PORT);

    /* ---------- Accept mot client ---------- */
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (client_fd < 0) {
        perror("[Server] Accept failed");
        close(server_fd);
        return 1;
    }
    printf("[Server] Client connected! (IP: %s, Port: %d)\n",
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    /* ---------- Vong lap trao doi tin nhan ---------- */
    while (1) {
        /* 1. Nhan tin nhan tu client */
        memset(buffer, 0, BUFSIZE);
        bytes = recv(client_fd, buffer, BUFSIZE - 1, 0);
        if (bytes <= 0) {
            printf("[Server] Client disconnected.\n");
            break;
        }
        printf("[Client says]: %s\n", buffer);

        /* Kiem tra lenh quit */
        if (strcmp(buffer, "quit") == 0) {
            printf("[Server] Client requested quit. Shutting down.\n");
            break;
        }

        /* 2. Server gui phan hoi */
        printf("[Server] Enter reply: ");
        fgets(buffer, BUFSIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';   /* xoa ky tu xuong dong */

        send(client_fd, buffer, strlen(buffer), 0);

        if (strcmp(buffer, "quit") == 0) {
            printf("[Server] Server requested quit. Shutting down.\n");
            break;
        }
    }

    /* ---------- Don dep ---------- */
    close(client_fd);
    close(server_fd);
    printf("[Server] Closed.\n");
    return 0;
}
