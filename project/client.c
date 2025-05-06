
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9000
#define MAX_BUFFER 8192

void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void recv_prompt(int sock, char *prompt, size_t size) {
    int len = recv(sock, prompt, size - 1, 0);
    if (len <= 0) {
        fprintf(stderr, "[!] Server closed connection or timeout\n");
        exit(EXIT_FAILURE);
    }
    prompt[len] = '\0';
    printf("%s", prompt);
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[MAX_BUFFER], input[256], filename[256], full_filename[512];
    char username[50], password[50];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) error_exit("Socket creation failed");

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    printf("Connecting to server...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        error_exit("Connection failed");
    }

    recv_prompt(sock, buffer, sizeof(buffer)); // "Username:"
    fgets(username, sizeof(username), stdin);
    username[strcspn(username,"\n")] = 0;
    send(sock, username, strlen(username), 0);

    recv_prompt(sock, buffer, sizeof(buffer)); // "Password:"
    fgets(password, sizeof(password), stdin);
    password[strcspn(password,"\n")] = 0;
    send(sock, password, strlen(password), 0);

    int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        fprintf(stderr, "[!] No response after login\n");
        close(sock);
        return 1;
    }
    buffer[len] = '\0';

    if (strstr(buffer, "failed")) {
        printf("[!] Authentication failed.\n");
        close(sock);
        return 1;
    }

    printf("[+] Login successful. You can upload, download or exit.\n");

    while (1) {
        printf("\nChoose option:\n1. Upload\n2. Download\n3. Exit\nChoice: ");
        fgets(input, sizeof(input), stdin);

        if (strncmp(input, "1", 1) == 0) {
            send(sock, "upload", strlen("upload"), 0);
            printf("Enter filename to upload: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = 0;

            if (strlen(filename) > 200) filename[200] = '\0';

            send(sock, filename, strlen(filename), 0);

            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                perror("File open failed");
                continue;
            }

            while (!feof(fp)) {
                int n = fread(buffer, 1, MAX_BUFFER, fp);
                if (n > 0)
                    send(sock, buffer, n, 0);
            }
            fclose(fp);
            send(sock, "EOF", 3, 0);
            printf("[+] Upload complete.\n");

        } else if (strncmp(input, "2", 1) == 0) {
            send(sock, "download", strlen("download"), 0);
            recv_prompt(sock, buffer, sizeof(buffer));
            printf("Enter filename to download: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = 0;

            if (strlen(filename) > 200) filename[200] = '\0';

            send(sock, filename, strlen(filename), 0);

            mkdir("downloads", 0777);
            snprintf(full_filename, sizeof(full_filename), "downloads/%s", filename);
            FILE *fp = fopen(full_filename, "wb");
            if (!fp) {
                perror("File create failed");
                continue;
            }

            while ((len = recv(sock, buffer, MAX_BUFFER, 0)) > 0) {
                if (strncmp(buffer, "EOF", 3) == 0)
                    break;
                fwrite(buffer, 1, len, fp);
            }
            fclose(fp);
            printf("[+] Download complete. Saved to downloads/%s\n", filename);

        } else if (strncmp(input, "3", 1) == 0) {
            printf("Exiting...\n");
            break;
        } else {
            printf("[!] Invalid choice. Try again.\n");
        }
    }

    close(sock);
    return 0;
}
