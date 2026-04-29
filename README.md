# Penjelasan kode

## protocol.h

```c
#define PORT            8081
#define MAX_CLIENTS     100
#define BUFFER_SIZE     1024
#define NAME_SIZE       32
#define ADMIN_PASS      "protocol7"

#define CMD_EXIT        "/exit"
```

Bagian ini mendefinisikan konfigurasi utama sistem. PORT menentukan port server yang dipakai, MAX_CLIENTS membatasi jumlah koneksi aktif, BUFFER_SIZE dan NAME_SIZE mengatur ukuran buffer komunikasi dan nama user. ADMIN_PASS adalah password khusus untuk role admin (The Knights), sedangkan CMD_EXIT dipakai sebagai command standar untuk keluar dari server.  

```c
typedef struct {
    int socket;
    char username[NAME_SIZE];
    int is_admin;
} Client;
```

Struct Client merepresentasikan setiap user yang terhubung ke server. Di dalamnya ada socket descriptor untuk komunikasi, username untuk identitas, dan flag is_admin untuk membedakan user biasa dan admin.  

```c
extern Client clients[MAX_CLIENTS];
extern int client_count;
extern pthread_mutex_t clients_mutex;
extern time_t server_start_time;
```

Bagian ini adalah shared state global yang dipakai lintas file. clients menyimpan semua client aktif, client_count menghitung jumlahnya, clients_mutex digunakan untuk sinkronisasi thread agar aman saat akses bersamaan, dan server_start_time dipakai untuk menghitung uptime server.  

```c
void broadcast(const char *message, int sender_sock);
void send_to_client(int sock, const char *message);
void *handle_client(void *arg);
```

Deklarasi fungsi utama sistem. broadcast digunakan untuk mengirim pesan ke semua client kecuali pengirim, send_to_client untuk komunikasi satu arah ke client tertentu, dan handle_client adalah thread utama yang menangani setiap koneksi client secara paralel.  

## protocol.c

```c
Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
time_t server_start_time;
```

Ini adalah implementasi variabel global yang dideklarasikan di header. clients menyimpan daftar user aktif, client_count melacak jumlahnya, mutex memastikan akses thread-safe, dan server_start_time diset saat server hidup.  

```c
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", t);
}
```

Fungsi ini membuat timestamp dalam format log standar. Dipakai untuk setiap event di history.log agar semua aktivitas memiliki waktu kejadian yang konsisten.  

```c
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

Fungsi logging utama. Semua event seperti connect, disconnect, chat, dan admin RPC akan masuk ke file history.log. Formatnya mengikuti standar:  
timestamp + kategori + message.  

```c
int is_username_taken(const char *name)
```

Fungsi ini mengecek apakah username sudah dipakai oleh client lain yang sedang aktif. Digunakan untuk mencegah duplicate identity di sistem The Wired.  

```c
int add_client(int sock, const char *name, int is_admin)
```

Menambahkan client baru ke array clients. Menggunakan mutex agar aman dari race condition. Jika penuh (MAX_CLIENTS), maka koneksi ditolak.  

```c
void remove_client(int sock)
```

Menghapus client dari daftar aktif berdasarkan socket. Setelah ditemukan, array digeser agar tidak ada gap. Ini penting agar client_count tetap valid.  

```c
void broadcast(const char *message, int sender_sock)
```

Fungsi inti chat system. Mengirim pesan ke semua client kecuali pengirim. Digunakan untuk fitur chat real-time antar user.  

```c
void send_to_client(int sock, const char *message)
```

Wrapper sederhana untuk send(). Dipakai untuk komunikasi langsung ke satu client, seperti prompt atau response admin.  

## wired.c

```c
server_start_time = time(NULL);
log_event("System", "SERVER ONLINE");
```

Saat server dijalankan, waktu awal disimpan untuk uptime tracking dan langsung dicatat ke history.log sebagai SERVER ONLINE.

### handle_client (inti server logic)

```c
send_to_client(sock, "Enter your name: ");
recv(sock, name, NAME_SIZE-1, 0);
```

Bagian ini meminta username saat client pertama kali connect. Semua user wajib mengisi nama sebelum masuk ke sistem.  

```c
if (strcmp(name, "The Knights") == 0)
```

Blok ini mendeteksi apakah user adalah admin. Jika iya, sistem meminta password sebelum memberikan akses ke menu RPC khusus admin.  

```c
if (strcmp(pass, ADMIN_PASS) == 0)
```

Validasi password admin. Jika benar, client diberi akses console admin The Knights, jika salah koneksi langsung ditutup.  

```c
if (is_username_taken(name))
```

Validasi duplicate username. Jika username sudah ada, client ditolak agar tidak terjadi konflik identitas di sistem chat.  

```c
add_client(sock, name, 0);
```

Menambahkan user biasa ke daftar client aktif. Setelah itu user akan mendapatkan pesan welcome ke The Wired.  

```c
log_event("System", "User '%s' connected");
```

Semua koneksi user biasa dicatat ke history.log agar sistem bisa tracking siapa yang masuk.  

### Main loop komunikasi client

```c
int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
```

Loop utama yang terus menerima input dari client. Jika bytes <= 0 berarti client disconnect atau error.  

```c
if (strcmp(buffer, CMD_EXIT) == 0)
```

Jika user mengetik /exit atau disconnect, loop dihentikan dan proses logout dimulai.  

### Admin command handling

```c
if (strcmp(buffer, "1") == 0)
log_event("Admin", "RPC_GET_USERS");
```

Admin menu option 1: menghitung jumlah user aktif. Semua aktivitas admin dicatat sebagai RPC_GET_USERS.  

```c
else if (strcmp(buffer, "2") == 0)
```

Admin option 2: menghitung uptime server berdasarkan selisih waktu sekarang dan server_start_time.  

```c
else if (strcmp(buffer, "3") == 0)
```

Admin option 3: emergency shutdown. Server akan broadcast shutdown message lalu exit(0), memutus semua koneksi.  

### Chat system non-admin

```c
snprintf(fullmsg, sizeof(fullmsg), "[%s]: %s\n", name, buffer);
broadcast(fullmsg, sock);
```

Ini inti sistem chat. Semua pesan user dikirim ke seluruh client lain secara real-time.  

```c
log_event("User", logmsg);
```

Setiap chat juga dicatat ke history.log agar semua komunikasi bisa dilacak.  

### Disconnect handling

```c
send_to_client(sock, "\n[System] Disconnecting from The Wired...\n");
```

Pesan ini dikirim saat user keluar, baik normal exit maupun Ctrl+C (SIGINT dari client).  

```c
remove_client(sock);
close(sock);
```

Client dihapus dari list aktif dan socket ditutup agar tidak terjadi memory/socket leak.  

### main() server

```c
server_sock = socket(AF_INET, SOCK_STREAM, 0);
```

Membuat TCP socket server.  

```c
bind(server_sock, ...)
listen(server_sock, MAX_CLIENTS);
```

Server bind ke port 8081 dan mulai listening untuk koneksi client.  

```c
pthread_create(&thread_id, NULL, handle_client, (void*)new_sock);
```

Setiap client dijalankan di thread terpisah agar server non-blocking dan bisa menangani banyak client sekaligus.  

## navi.c

```c
signal(SIGINT, handle_sigint);
```

Menangkap Ctrl+C di client agar tidak langsung keluar, tapi mengirim CMD_EXIT ke server terlebih dahulu.  

```c
pthread_create(&recv_thread, NULL, receive_messages, &sock);
```

Thread ini khusus untuk menerima pesan dari server secara async. Ini yang memungkinkan chat real-time tanpa blocking input user.  

## receive_messages

```c
int bytes = recv(sock, buffer, BUFFER_SIZE-1, 0);
printf("%s", buffer);
```

Loop ini terus menerima broadcast dari server dan langsung menampilkannya ke terminal user.  

## input loop client

```c
fgets(buffer, BUFFER_SIZE, stdin);
send(sock, buffer, strlen(buffer), 0);
```

Bagian ini mengirim input user ke server. Bisa chat biasa atau command seperti /exit.  

```c
if (strcmp(buffer, CMD_EXIT) == 0)
```

Jika user mengetik /exit, client akan mengirim sinyal keluar ke server lalu menutup koneksi.

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
   



   

   
   

   





   
   

