#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>


#define PORT 9000
#define MAX_CLIENTS 5
#define MAX_BUFFER 8192
#define AUTH_FILE "./common/auth.txt"

// Structure to hold client data
typedef struct {
    int socket;
    char username[50];
} client_data_t;

// Function to check authentication
int authenticate_user(int client_socket, char *username) {
    char recv_data[MAX_BUFFER], user_pass[4096], line[100];
    FILE *fp = fopen(AUTH_FILE, "r");
    if (!fp) return 0;

    send(client_socket, "Username: ", strlen("Username: "), 0);
    recv(client_socket, username, 50, 0);
    username[strcspn(username, "\n")] = 0;

    send(client_socket, "Password: ", strlen("Password: "), 0);
    recv(client_socket, recv_data, MAX_BUFFER, 0);
    recv_data[strcspn(recv_data, "\n")] = 0;

    int written = snprintf(user_pass, sizeof(user_pass), "%s:%s", username, recv_data);
    if (written >= sizeof(user_pass))
        fprintf(stderr, "[!] Warning: user_pass was truncated!\n");
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, user_pass) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

// Function to log client actions
void log_action(const char *action, const char *username) {
    pid_t pid = fork();
    if (pid == 0) {
        FILE *logfile = fopen("./server/logs/server_log.txt", "a");
        if (logfile) {
            fprintf(logfile, "User: %s | Action: %s\n", username, action);
            fclose(logfile);
        }
        exit(0);
    } else {
        wait(NULL);
    }
}

void *client_handler(void *arg) {
    client_data_t *client_data = (client_data_t *)arg;
    char buffer[MAX_BUFFER], filepath[MAX_BUFFER], filename[256];
    int bytes_read;

    log_action("Client connected", client_data->username);

    while (1) {
        bzero(buffer, MAX_BUFFER);
        if ((bytes_read = recv(client_data->socket, buffer, MAX_BUFFER, 0)) <= 0) break;

        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "upload") == 0) {
            recv(client_data->socket, filename, sizeof(filename), 0);
            filename[strcspn(filename, "\n")] = 0;
            snprintf(filepath, sizeof(filepath), "./server/server_files/%s_%s", client_data->username, filename);
            int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) continue;
            while ((bytes_read = recv(client_data->socket, buffer, MAX_BUFFER, 0)) > 0) {
                buffer[bytes_read] = '\0';
                if (strcmp(buffer, "EOF") == 0) break;
                write(fd, buffer, bytes_read);
            }
            close(fd);
            log_action("File uploaded", client_data->username);
        } else if (strcmp(buffer, "download") == 0) {
                DIR *dir = opendir("./server/server_files");
                struct dirent *entry;
                char file_list[MAX_BUFFER] = "Available files:\n";
                if (dir) {
                        while ((entry = readdir(dir)) != NULL) {
                                // Skip . and ..
                                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                                        continue;
                                char fullpath[PATH_MAX];
                                snprintf(fullpath, sizeof(fullpath), "./server/server_files/%s", entry->d_name);
                                struct stat path_stat;
                                stat(fullpath, &path_stat);
                                if (!S_ISDIR(path_stat.st_mode)) {
                                        strcat(file_list, entry->d_name);
                                        strcat(file_list, "\n");
                                }
                        }
                        closedir(dir);
                } else {
                        strcpy(file_list, "Could not list files.\n");
                }
                send(client_data->socket, file_list, strlen(file_list), 0);
                send(client_data->socket, "Enter filename to download:\n", 29, 0);

                recv(client_data->socket, filename, sizeof(filename), 0);
                filename[strcspn(filename, "\n")] = 0;

                snprintf(filepath, sizeof(filepath), "./server/server_files/%s", filename);
                struct stat path_stat;
                if (stat(filepath, &path_stat) < 0 || S_ISDIR(path_stat.st_mode)) {
                        send(client_data->socket, "Error: File does not exist or is a directory.\n", 46, 0);
                        continue;
                }
                int fd = open(filepath, O_RDONLY);
                if (fd < 0) {
                        send(client_data->socket, "Error opening file.\n", 21, 0);
                        continue;
                }
                while ((bytes_read = read(fd, buffer, MAX_BUFFER)) > 0) {
                        send(client_data->socket, buffer, bytes_read, 0);
                }
                send(client_data->socket, "EOF", 3, 0);
                close(fd);
                log_action("File downloaded", client_data->username);
        }
        else {
            send(client_data->socket, "Invalid command\n", 17, 0);
        }
    }

    close(client_data->socket);
    log_action("Client disconnected", client_data->username);
    free(client_data);
    pthread_exit(NULL);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t tid;

    mkdir("./server", 0777);
    mkdir("./server/server_files", 0777);
    mkdir("./server/logs", 0777);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) exit(1);

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) exit(1);
    if (listen(server_fd, MAX_CLIENTS) < 0) exit(1);

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        client_data_t *data = malloc(sizeof(client_data_t));
        data->socket = client_fd;

        if (!authenticate_user(client_fd, data->username)) {
            send(client_fd, "Authentication failed\n", 23, 0);
            close(client_fd);
            free(data);
            continue;
        }
        else {
        send(client_fd, "Authentication successful\n", 27, 0);
        }
        pthread_create(&tid, NULL, client_handler, (void *)data);
        pthread_detach(tid);
    }
    close(server_fd);
    return 0;
}
