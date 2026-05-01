# Penjelasan Kode 

## Protocol.h
Berisi semua konstanta, struct dan prototipe fungsi, bertujuan supaya:
- Server & client memakai aturan yang sama
- Tidak perlu mendefinisikan ulang

### Library yang dipakai
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
```
`arpa/inet.h` untuk socket networking  
`pthread.h` untuk multi-thread (server handle banyak client)  
`time.h` logging timestamp  
`signal.h` handle Ctrl+C (server shutdown / client exit)

### Konstanta Global
```
#define PORT 8081
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define NAME_SIZE 32
#define ADMIN_PASS "protocol7"
```
Server menggunakan port 8081 dengan batas maksimum 100 koneksi, ukuran pesan yang dikirimkan maksimal 1024, ukuran nama maksimal 32 dan khusus `ADMIN_PASS` merupakan password untuk The Knights

### Tipe command & pesan log
```c
#define CMD_EXIT "/exit"

#define TYPE_CHAT "[CHAT]"
#define TYPE_SYSTEM "[SYSTEM]"
#define TYPE_ADMIN "[ADMIN]"
```
Di sini terdapat definisi untuk command exit, lalu setiap input akan dibedakan apakah berasal dari user, system atau admin

### Struct client
```c
typedef struct {
    int socket;
    char username[NAME_SIZE];
    int is_admin;
} Client;
```
Terdapat atribut `int is_admin` karena user akan dibedakan menjadi user biasa maupun admin, untuk The Knights nantinya akan tetap dianggap user, namun dengan label admin.

### Global variable extern
```c
extern Client clients[MAX_CLIENTS];
extern int client_count;
extern pthread_mutex_t clients_mutex;
extern time_t server_start_time;
```
Disebut extern karena variabel ini akan didefinisikan di file .c lain 
- Array `clients[]` merupakan list semua user aktif
- `client_count` merupakan jumlah user
- `clients_mutex` nantinya akan digunakan dalam aplikasi mutex pada critical section
- `server_start_time` untuk fitur The Knights yang memeriksa lama server aktif

### Prototipe fungsi
```c
void get_timestamp(char *buffer, size_t size);
void log_event(const char *category, const char *message);

int is_username_taken(const char *name);
int add_client(int sock, const char *name, int is_admin);
void remove_client(int sock);

void broadcast(const char *message, int sender_sock);
void send_to_client(int sock, const char *message);

void *handle_client(void *arg);  // fungsi untuk thread
void *handle_admin_command(int sock, const char *name);
```
Di sini ada fungsi untuk timestamp dan menulis log, lalu ada fungsi untuk periksa nama client yang sama, fungsi untuk menambahkan client saat terhubung dan menghapus client saat disconnect. Lalu ada broadcast untuk mengirim pesan ke semua client sekaligus, ada juga fungsi send_to_client yang secara konsep mirip DM. handle_client marupakan fungsi thread masing-masing user dan handle_admin_command merupakan sekumpulan fungsi ketika user sedang login sebagai The Knights

## Protocol.c
```c
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
```
Segmen ini secara garis besar untuk implementasi dari bagian komunikasinya, ada timestamp untuk ditambahkan ke format penulisan log, lalu ada `log_event` yang tugas utamanya menulis terformat ke history.log.  

```c
int is_username_taken(const char *name) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, name) == 0)
            return 1;
    }
    return 0;
}
```
Ada fungsi is_username_taken untuk memastikan user yang akan connect tidak memiliki nama yang sam adengan user lain yang connect lebih dulu.  

```c
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
```
Selanjutnya ada add client menggunakan lock mutex. Mula mula lock diawal untuk masuk validasi dulu, jika client count melebihi kapasitas server, maka mutex langsung unlock dan return -1, dilanjut mengisi data seperti socket, username dan is_admin, lalu client_count diincrement untuk terakhir di unlock mutex dan return 0.  

```c
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
```
Lalu ada remove_client, mula mula lock mutex dulu lalu akan loop sebanyak client_count, kalau ada isi socket client yang sama dari sock ( yang akan dihapus ), kita menggunakan mekanisme delete array.

```c
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
```
Ada juga implementais broadcast, jadi di lock dulu, looping sebanyak jumlah user dan kalau socketnya beda dengan milik pengirim, pesan akan diteruskan. Terakhir ada send_to_client, fungsi khusus yang memasukkan suatu pesan ke suatu sock

## Wired.c
### Fungsi besar Handle client()
```c
    int sock = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    char name[NAME_SIZE] = {0};
    int is_admin = 0;
```
`handle_client()` merupakan fungsi utama yang berjalan pada thread setiap client, fungsi ini memiliki argumen `arg` yang akan dikonversi menjadi int karena sock dideklarasi dalam bentuk int. array `buffer` menampung banyak pesan masuk sesuai kapasitasnya, begitu juga array `name`, untuk is_admint diinisialisasi menjadi nol, berubah 1 jika user yang connect adalah The Knights

```c
send_to_client(sock, "Enter your name: ");
recv(sock, name, NAME_SIZE-1, 0);
name[strcspn(name, "\n")] = 0;
```
Di sini server akan mengirimkan pesan "Enter your name" untuk dijadikan output pada tampilan client, server akan menerima nama yang akan disimpan dalam sock, newline di akhir harus dibersihkan dengan menggantinya dengan 0.

### If else pertama Handle client()
If else pertama menentukan apakah user login pertama merupakan "The Knights" atau bukan.  
```c
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
```
Jika user yang connect adalah The Knights, server akan mengirimkan teks output "Enter password" untuk ditampilkan ke client, server akan menerima password yang diketik dari client dan langsung membersihkan newlinenya. Jika password yang diinput sama dengan ADMIN_PASS yang sudah dideklarasi pada `protocol.h`,  maka ubah is_admin menjadi 1, kirim output menu dan catat ke log sebagai [System] jika admin sudah terhubung, bagian else adalah error handling jika password salah.

```c
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
```
Bagian ini berjalan jika user yang connect adalah selain "The Knights" atau disebut user biasa.  

Mula mula akan dipanggil fungsi `is_username_taken()` untuk memeriksa user double, jika nama yang diinput sudah pernah dipakai user lain maka keluarkan pesan dan langsung close sock.  

Lalu dipanggil fungsi add client, return akan berupa fd sock client sehingga jika nilainya kurang dari nol akan dianggap gagal atau server penuh, jika gagal maka langsung close sock dan return NULL.

Buat array berisi ucapan welcome to the wireddan kirim ke client  

Terakhir, tambahkan history client yang baru terhubung ke log, tulis terformat menggunakan sprintf, catat juga kalau log ditulis oleh [System]

### While loop Handle client()
Dalam while loop terdapat eksekusi perintah, masuk if jika perintah berasal dari is_admin, di luarnya merupakan eksekusi perintah untuk chat user biasa  

```c
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
            } else if (strstr(buffer, "3") || strstr(buffer, "shutdown")) {
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
```
Mula mula if yang menghandle perintah dari admin. Mekanismenya, user akan menginputkan "The Knights", hasil input masuk ke buffer dan oleh sistem dibersihkan elemen `\r\n` supaya terhindar dari invalid input padahal yang diinputkan sudah benar. Selanjutnya dilakukan penulisan log sesuai ketentuan berdasakan angka yang diinputkan

Pada if else di bawahnya merupakan eksekusi per masing masing menu The Knights:
- Menu 1 adalah untuk melihat jumlah user aktif yang secara tidak langsung akan menampilkan client_count, namun client_count akan dibuatkan buffer array dengan nama `resp` untuk membuat output terformat sebelum dikirimkan ke user.
- Menu 2 akan menampilkan berapa lama server telah aktif, menggunakan tipe data `time_t` dan fungsi `difftime`. Server akan menulis terformat di buffer `resp` sebelum dikirim ke client
- Menu 3 menjalankan emergency shutdown oleh admin, sistem akan menuliskan log sesuai dan membuat broadcast untuk semua user bahwa server telah dimatikan, sleep satu detik untuk memberi jeda dan memastikan pesan broadcast telah terkirim.
- Jika Admin memilih nomor 4, akan pada client terkait (yang dalam hal ini adalah admin) akan muncul output disconnect, lalu break untuk keluar dari program. Selain menu 4 yang akan menjalankan break, pesan akan meminta command terus menerus karena bagian ini berada di dalam loop while.

```c
        char fullmsg[BUFFER_SIZE + NAME_SIZE + 30];
        snprintf(fullmsg, sizeof(fullmsg), "[%s]: %s\n", name, buffer);
        broadcast(fullmsg, sock);
        fullmsg[strcspn(fullmsg, "\n")] = 0;

        char logmsg[BUFFER_SIZE + 50];
        snprintf(logmsg, sizeof(logmsg), "[%s]: %s", name, buffer);
        log_event("User", logmsg);
```
Masih di dalam loop while, namun bagian ini di luar `if(is_admin)` yang akan berjalan untuk user biasa, segmen pertama membuat pesan terformat dalam buffer fullmsg yang akan menyusun [nama user]<pesan> untuk ditampilkan saat pesan terkirim, pesan akan masuk ke log setelah terkirim, namun dengan keterangan [User]

```c
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
```
Ini sudah bagian terakhir fungsi besar handle_client(), namun di luar while loop karena bagian ini dijalankan saat terdapat client yang disconnect. Sistem akan menuliskan log sebelum menutup socket dan keluar dari fungsi. Yay

### Main()
```
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t thread_id;
```
Bagian ini merupakan deklarasi sock, address, panjang address client dan juga thread.
```c
int main() {
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
```
Terdapat server start time untuk mencatat log ketika server mulai diaktifkan.  

Fungsi di bawahnya merupakan prosedur server sock pada umumnya yang terdiri dari pembuatan socket, setsockopt() supaya sock reusable, konfigurasi alamat dan port, bind, listen lalu terdapat loop while yang di dalamnya ada accept. Semuanya sudah disertai pesan error jika sampai ada step yang gagal.

Di dalam loop while pada main, `client_sock` menerima informasi dari `server_sock`. Begitu file .c untuk server dijalankan, server secara otomatis akan mengalokasikan memori untuk membuat thread baru. Jika gagal, memori sock yang baru dibuat akan dibebaskan dan thread akan didetach atau dilepaskan.

## Navi.c
### Fungsi sebelum main
```c
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
```
Sebelumnya pada `protocol.h`, sudah dideklarasikan bahwa CMD_EXIT adalah jika user mengetikkan `/exit`  

Lalu ada fungsi untuk menerima pesan, yang akan dijalankan oleh thread tiap-tiap user. Mula mula inisiasi sock yang sebelumnya merupakan konversi parameter arg dari void ke int, lalu ada deklarasi buffer dengan ukuran yang sebelumnya sudah ditetapkan pada `protocol.h`. Di dalam fungsi while, untuk menerima pesan harus mengalokasikan memori baru, lalu menjalankan `recv()` untuk menerima pesan dan terdapat validasi jika recv gagal

## Main()
```c

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
```
Di dalam main terdapat inisialisasi fungsi signal() untuk kasus user yang mengetik /exit, lalu ada inisialisasi sock, address, thread dan buffer

Selanjutnya dijalankan prosedur socket untuk client seperti membuat sock dengan fungsi `socket()`, inisialisasi alamat dan port, `connect()`, lalu buat thread dan lanjut detach, tidak menggunakan join karena thread akan terus menerima pesan selama server aktif, main mungkin tidak berjalan sewaktu waktu yang menyebabkan user tertentu bahkan tidak bisa diinput lagi.

Di dalam fungsi while, terdapat alokasi buffer untuk menyimpan input yang diambil menggunakan `fgets()`  

bagian selanjutnya adalah kondisi jika user menginput `./exit` yang membuat user tersebut langsung berhenti dari program yang dijalankan, jika tidak berhenti karena di-break maka pesan akan dikirimkan ke server untuk ditangani lebih lanjut, lalu close sock dan return.


## Output

1. Compile dan jalankan `wired.c`
   <br>
   <img width="992" height="360" alt="image" src="https://github.com/user-attachments/assets/812c184e-9fd3-4009-a6e6-61e994993459" />
   <br>
   <img width="825" height="391" alt="image" src="https://github.com/user-attachments/assets/9f072eb1-824c-47f1-8c88-b1174abfcb54" />
   <br>
2. Compile dan jalankan `navi.c` di dua terminal untuk login sebagai dua user non-admin
   <br>
   <img width="1614" height="536" alt="image" src="https://github.com/user-attachments/assets/aedd4fbd-a48d-46e4-b3cf-72019870db57" />
   <br>
   Jika ada nama yang double
   <br>
   <img width="661" height="178" alt="image" src="https://github.com/user-attachments/assets/023ffd41-b06f-466a-81d6-5029949da946" />
   <br>
3. Login sebagai The Knights, pilih opsi 1. Check active users
   <br>
   <img width="812" height="404" alt="image" src="https://github.com/user-attachments/assets/13eadb40-40ca-49a3-a86f-dd03fe28024d" />
   <br>
   Tampilan log
   <br>
   <img width="990" height="298" alt="image" src="https://github.com/user-attachments/assets/412a441e-a12f-47a9-bb6c-a4d7e2072187" />
   <br>
4. Exit salah satu user
   <br>
   <img width="1306" height="588" alt="image" src="https://github.com/user-attachments/assets/69b18b6c-b9e6-43b2-9b34-0e8015519a5a" />
   <br>
   Cek jumlah user pada The Knights
   <br>
   <img width="841" height="536" alt="image" src="https://github.com/user-attachments/assets/683bbc59-08c5-459d-a22a-f88d4b32f1d1" />
   <br>
   Cek log
   <br>
   <img width="1084" height="305" alt="image" src="https://github.com/user-attachments/assets/f3410e6b-f4b5-4d48-8160-07802992c70e" />
   <br>
6. Coba The Knights fitur kedua
   <br>
   <img width="801" height="454" alt="image" src="https://github.com/user-attachments/assets/1a92f42f-937e-40a0-8882-09d4b89e79e3" />
   <br>
   Cek log
   <br>
   <img width="1130" height="353" alt="image" src="https://github.com/user-attachments/assets/a4a1217f-7843-48b5-ab1a-716197f8051c" />
   <br>
8. Coba The Knights fitur keempat, disconnect
   <br>
   <img width="858" height="462" alt="image" src="https://github.com/user-attachments/assets/236a6c6b-af04-4cef-930f-7ed0acdcb2b3" />
   <br>
9. Login lagi sebagai The Knight unutuk coba fitur ketiga
    <br>
    <img width="913" height="359" alt="image" src="https://github.com/user-attachments/assets/bce39042-ede1-4ae1-b13a-d4311be42e93" />
    <br>
    Tampilan log dan server lain yang masih aktif
   <br>
   <img width="1158" height="428" alt="image" src="https://github.com/user-attachments/assets/4972bf42-54c9-4b2e-b59b-2d9dc5c2e716" />
   



   

   
   

   





   
   

