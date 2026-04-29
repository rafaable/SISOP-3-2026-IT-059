#include "protocol.h"

void *handle_client(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    char name[NAME_SIZE] = {0};
    int is_admin = 0;

    // === Minta Nama ===
    send_to_client(sock, "Enter your name: ");
    recv(sock, name, NAME_SIZE-1, 0);
    name[strcspn(name, "\n")] = 0;

    // ==================== ADMIN (The Knights) ====================
    if (strcmp(name, "The Knights") == 0) {
        send_to_client(sock, "Enter Password: ");
        char pass[64] = {0};
        recv(sock, pass, sizeof(pass)-1, 0);
        pass[strcspn(pass, "\n")] = 0;

        if (strcmp(pass, ADMIN_PASS) == 0) {
            is_admin = 1;
            send_to_client(sock, "\n[System] Authentication Successful. Granted Admin privileges.\n\n");
            send_to_client(sock, "\n=== THE KNIGHTS CONSOLE ===\n");
            send_to_client(sock, "1. Check Active Entities (Users)\n");
            send_to_client(sock, "2. Check Server Uptime\n");
            send_to_client(sock, "3. Execute Emergency Shutdown\n");
            send_to_client(sock, "4. Disconnect\n");
            send_to_client(sock, "Command >> ");

            log_event("System", "Admin 'The Knights' connected");
        } else {
            send_to_client(sock, "[System] Authentication Failed.\n");
            close(sock);
            return NULL;
        }
    } 
    // ==================== USER BIASA ====================
    else {
        if (is_username_taken(name)) {
            send_to_client(sock, "[System] The identity '");
            send_to_client(sock, name);
            send_to_client(sock, "' is already synchronized in The Wired.\n");
            close(sock);
            return NULL;
        }

        if (add_client(sock, name, 0) < 0) {
            send_to_client(sock, "[System] Server is full.\n");
            close(sock);
            return NULL;
        }

        char welcome[128];
        snprintf(welcome, sizeof(welcome), "--- Welcome to The Wired, %s ---\n", name);
        send_to_client(sock, welcome);

        char logmsg[150];
        snprintf(logmsg, sizeof(logmsg), "User '%s' connected", name);
        log_event("System", logmsg);
    }

    // ====================== MAIN LOOP ======================
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);

        if (bytes <= 0) break;                    // Client disconnect (termasuk Ctrl+C)

        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) == 0) continue;

        if (strcmp(buffer, CMD_EXIT) == 0) {
            break;
        }

        // ==================== ADMIN COMMAND ====================
        if (is_admin) {
            buffer[strcspn(buffer, "\r\n")] = 0;
            if (strcmp(buffer, "1") == 0) log_event("Admin", "RPC_GET_USERS");
            else if (strcmp(buffer, "2") == 0) log_event("Admin", "RPC_GET_UPTIME");
            else if (strcmp(buffer, "3") == 0) log_event("Admin", "RPC_SHUTDOWN");

            if (strstr(buffer, "1") || strstr(buffer, "users")) {
                int active_users = client_count;
                char resp[100];
                snprintf(resp, sizeof(resp), "[Admin] Active users: %d\n", active_users);
                send_to_client(sock, resp);
            } else if (strcmp(buffer, "2") == 0) {
                time_t now = time(NULL);
                int uptime = (int)difftime(now, server_start_time);
                char resp[128];
                snprintf(resp, sizeof(resp),
                "[Admin] Server uptime: %d seconds\n", uptime);
                send_to_client(sock, resp);
            }else if (strstr(buffer, "3") || strstr(buffer, "shutdown")) {
                log_event("System", "EMERGENCY SHUTDOWN INITIATED");
                broadcast("[System] Server is shutting down by Admin...\n", -1);
                sleep(1);
                exit(0);
            } else if (strcmp(buffer, "4") == 0) {
                send_to_client(sock, "[System] Disconnecting from The Wired...\n");
                break;
            }

            send_to_client(sock, "Command >> ");
            continue;
        }

        // ==================== CHAT BIASA (Non-Admin) ====================
        char fullmsg[BUFFER_SIZE + NAME_SIZE + 30];
        snprintf(fullmsg, sizeof(fullmsg), "[%s]: %s\n", name, buffer);

        broadcast(fullmsg, sock);
        fullmsg[strcspn(fullmsg, "\n")] = 0;
        char logmsg[BUFFER_SIZE + 50];
        snprintf(logmsg, sizeof(logmsg), "[%s]: %s", name, buffer);
        log_event("User", logmsg);
    }

    // ====================== DISCONNECT HANDLING ======================
    if (is_admin) {
        send_to_client(sock, "\n[System] Disconnecting from The Wired...\n");
        log_event("System", "Admin 'The Knights' disconnected");
    } else {
        char logmsg[150];
        snprintf(logmsg, sizeof(logmsg), "User '%s' disconnected", name);
        log_event("System", logmsg);
    }

    remove_client(sock);
    close(sock);
    return NULL;
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t thread_id;

    server_start_time = time(NULL);
    log_event("System", "SERVER ONLINE");

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("The Wired Server is running on port %d...\n", PORT);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) continue;

        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
        }
        pthread_detach(thread_id);
    }

    close(server_sock);
    return 0;
}