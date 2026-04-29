#include "protocol.h"
#include <signal.h>

int sock_global;

void handle_sigint(int sig) {
    send(sock_global, CMD_EXIT, strlen(CMD_EXIT), 0);
}

void *receive_messages(void *arg) {
    int sock = *(int*)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(sock, buffer, BUFFER_SIZE-1, 0);
        if (bytes <= 0) {
            break;
        }
        printf("%s", buffer);
        fflush(stdout);
    }
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);
    int sock;
    sock_global = sock;
    struct sockaddr_in server_addr;
    pthread_t recv_thread;
    char buffer[BUFFER_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    // Thread untuk menerima pesan
    pthread_create(&recv_thread, NULL, receive_messages, &sock);
    pthread_detach(recv_thread);

    // Input dari user
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;

        if (strcmp(buffer, CMD_EXIT) == 0 || strcmp(buffer, CMD_EXIT "\n") == 0) {
            send(sock, CMD_EXIT, strlen(CMD_EXIT), 0);
            break;
        }

        send(sock, buffer, strlen(buffer), 0);
    }

    close(sock);
    return 0;
}
