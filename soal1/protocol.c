#include "protocol.h"

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
time_t server_start_time;

void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", t);
}

void log_event(const char *category, const char *message) {
    char ts[64];
    get_timestamp(ts, sizeof(ts));
    
    FILE *f = fopen("history.log", "a");
    if (f) {
        fprintf(f, "%s [%s] [%s]\n", ts, category, message);
        fclose(f);
    }
}

int is_username_taken(const char *name) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, name) == 0)
            return 1;
    }
    return 0;
}

int add_client(int sock, const char *name, int is_admin) {
    pthread_mutex_lock(&clients_mutex);
    if (client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }
    clients[client_count].socket = sock;
    strncpy(clients[client_count].username, name, NAME_SIZE-1);
    clients[client_count].is_admin = is_admin;
    client_count++;
    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

void remove_client(int sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == sock) {
            
            // Geser array
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j+1];
            }
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast(const char *message, int sender_sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket != sender_sock) {
            send(clients[i].socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_to_client(int sock, const char *message) {
    send(sock, message, strlen(message), 0);
}