/*
 * IPC - Client Program (TCP Socket) - Linux Ubuntu
 * Compile: gcc client.c -o client
 * Run:     ./client
 *
 * Client kết nối tới server trên localhost:8080,
 * và trao đổi tin nhắn tương tác cho đến khi một bên gửi "quit".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT      8080
#define BUFSIZE   1024
#define SERVER_IP "127.0.0.1"

int main(void)
{
    int    sock;
    struct sockaddr_in server_addr;
    char   buffer[BUFSIZE];
    int    bytes;

    /* ---------- Tao socket ---------- */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[Client] Socket creation failed");
        return 1;
    }

    /* ---------- Ket noi toi server ---------- */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("[Client] Invalid address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Client] Connection failed");
        close(sock);
        return 1;
    }
    printf("[Client] Connected to server %s:%d\n", SERVER_IP, PORT);

    /* ---------- Vong lap trao doi tin nhan ---------- */
    while (1) {
        /* 1. Client gui tin nhan */
        printf("[Client] Enter message: ");
        fgets(buffer, BUFSIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';   /* xoa ky tu xuong dong */

        send(sock, buffer, strlen(buffer), 0);

        if (strcmp(buffer, "quit") == 0) {
            printf("[Client] Quit requested. Disconnecting.\n");
            break;
        }

        /* 2. Nhan phan hoi tu server */
        memset(buffer, 0, BUFSIZE);
        bytes = recv(sock, buffer, BUFSIZE - 1, 0);
        if (bytes <= 0) {
            printf("[Client] Server disconnected.\n");
            break;
        }
        printf("[Server says]: %s\n", buffer);

        if (strcmp(buffer, "quit") == 0) {
            printf("[Client] Server requested quit. Disconnecting.\n");
            break;
        }
    }

    /* ---------- Don dep ---------- */
    close(sock);
    printf("[Client] Closed.\n");
    return 0;
}
