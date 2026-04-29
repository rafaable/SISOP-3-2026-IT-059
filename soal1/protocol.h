#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#define PORT            8081
#define MAX_CLIENTS     100
#define BUFFER_SIZE     1024
#define NAME_SIZE       32
#define ADMIN_PASS      "protocol7"

#define CMD_EXIT        "/exit"

// Message types (prefix)
#define TYPE_CHAT       "[CHAT]"
#define TYPE_SYSTEM     "[SYSTEM]"
#define TYPE_ADMIN      "[ADMIN]"

typedef struct {
    int socket;
    char username[NAME_SIZE];
    int is_admin;
} Client;

extern Client clients[MAX_CLIENTS];
extern int client_count;
extern pthread_mutex_t clients_mutex;
extern time_t server_start_time;

// Fungsi umum
void get_timestamp(char *buffer, size_t size);
void log_event(const char *category, const char *message);
int is_username_taken(const char *name);
int add_client(int sock, const char *name, int is_admin);
void remove_client(int sock);
void broadcast(const char *message, int sender_sock);
void send_to_client(int sock, const char *message);
void *handle_client(void *arg);
void *handle_admin_command(int sock, const char *name);

#endif
