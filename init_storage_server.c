//init_storage_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netdb.h>
#define BUFFER_SIZE 2048
#define MAX_FILES 100
#define DIR_SIZE 1024
#define MAX_DIRS 500



typedef struct {
    char name[50];
    char path[100];
    int is_directory;  // 1 if directory, 0 if file
} FileInfo;

FileInfo files[MAX_FILES];
int file_count = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
int ss_no;
char nm_server_ip[INET_ADDRSTRLEN];
int n_port;

char* get_server_ip() {
    char hostname[1024];
    struct addrinfo hints, *res, *p;
    int status;
    static char ipstr[INET_ADDRSTRLEN];

    // Get the hostname
    if (gethostname(hostname, sizeof(hostname)) == -1) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }

    // Prepare hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    // Get address info
    if ((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    // Loop through results and pick the first non-loopback IP
    for(p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        // Convert the IP to a string
        inet_ntop(p->ai_family, &(ipv4->sin_addr), ipstr, sizeof ipstr);
        // Skip loopback addresses
        if (strcmp(ipstr, "127.0.0.1") != 0) {
            break;
        }
    }

    if (p == NULL) {
        fprintf(stderr, "Could not find a non-loopback IP address.\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res); // Free the linked list
    return ipstr;
}

char *get_file_name(char *path) {
    // Find the last occurrence of '/' in the path
    char *file_name = strrchr(path, '/');
    
    // If '/' is found, return the character after it; otherwise, return the path itself
    return (file_name != NULL) ? file_name : path;
}

// void *handle_client(void *arg) {
//     int client_socket = *((int *)arg);
//     free(arg);

//     char buffer[BUFFER_SIZE];
//     int read_size;

//     while ((read_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
//         buffer[read_size] = '\0';

//         // Parse command
//         char command[10], filename[50], content[BUFFER_SIZE];
//         sscanf(buffer, "%s %s %[^\n]", command, filename, content);

//         if (strcmp(command, "CREATE") == 0) {
//             handle_create_file(client_socket, filename);
//         } else if (strcmp(command, "READ") == 0) {
//             handle_read_file(client_socket, filename);
//         } else if (strcmp(command, "DELETE") == 0) {
//             handle_delete_file(client_socket, filename);
//         } else if (strcmp(command, "WRITE") == 0) {
//             handle_async_write(client_socket, filename, content);
//         } else {
//             send(client_socket, "Unknown Command\n", strlen("Unknown Command\n"), 0);
//         }
//     }

//     close(client_socket);
//     return NULL;
// }
void setup_storage_server(StorageServer *details, char *argv[2]) {
    int client_socket, naming_socket, new_client_socket, *new_sock;
    struct sockaddr_in client_addr, naming_addr, incoming_client_addr;
    socklen_t client_len = sizeof(incoming_client_addr);


    // Create socket for client connections
    // client_socket = socket(AF_INET, SOCK_STREAM, 0);
    // if (client_socket == -1) {
    //     perror("Could not create client socket");
    //     exit(EXIT_FAILURE);
    // }

    // // Set up client address structure
    // client_addr.sin_family = AF_INET;
    // client_addr.sin_addr.s_addr = inet_addr(details->ip_address);
    // client_addr.sin_port = htons(details->client_port);

    // // Bind the client socket to the specified port
    // if (bind(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
    //     perror("Client bind failed");
    //     close(client_socket);
    //     exit(EXIT_FAILURE);
    // }


    // listen(client_socket, 5);
   

    // Accept and handle client connections
    // while ((new_client_socket = accept(client_socket, (struct sockaddr *)&incoming_client_addr, &client_len))) {
    //     printf("Client connected.\n");

    //     pthread_t client_thread;
    //     new_sock = malloc(sizeof(int));
    //     *new_sock = new_client_socket;

    //     if (pthread_create(&client_thread, NULL, handle_client, (void *)new_sock) < 0) {
    //         perror("Could not create thread");
    //         return;
    //     }
    // }

    // if (new_client_socket < 0) {
    //     perror("Accept failed");
    //     close(client_socket);
    // }

    // close(client_socket);
    
}
void create_empty_file(char *filepath) {
    FILE *file = fopen(filepath, "w");
    if (file) {
        fclose(file);
        printf("Created empty file: %s\n", filepath);
        char* filename = get_file_name(filepath);
        // Append to the file list
        snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", filename);
        snprintf(files[file_count].path, sizeof(files[file_count].path), "%s", filepath);
        files[file_count].is_directory = 0;
        file_count++;
    } else {
        perror("Error creating file");
    }
}

// Function to delete a file from the storage server directory
void delete_file(char *filepath) {
    if (remove(filepath) == 0) {
        printf("Deleted file: %s\n", filepath);
        char* filename = get_file_name(filepath);
        // Remove file from file list
        for (int i = 0; i < file_count; i++) {
            if (strcmp(files[i].name, filename) == 0 && files[i].is_directory == 0) {
                for (int j = i; j < file_count - 1; j++) {
                    files[j] = files[j + 1];
                }
                file_count--;
                break;
            }
        }
    } 
    else {
        perror("Error deleting file");
    }
}



// Function to create a new directory
void create_directory(char *dirpath) {
    if (mkdir(dirpath, 0777) == 0) {
        printf("Created directory: %s\n", dirpath);
        char* dirname = get_file_name(dirpath);
        // Add directory to file list
        snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", dirname);
        snprintf(files[file_count].path, sizeof(files[file_count].path), "%s", dirpath);
        files[file_count].is_directory = 1;
        file_count++;
    } else {
        perror("Error creating directory");
    }
}

// Function to delete a directory and its contents recursively
void delete_directory(char *dirpath) {
    DIR *d = opendir(dirpath);
    struct dirent *entry;
    if (d) {
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char entrypath[500];
                snprintf(entrypath, sizeof(entrypath), "%s/%s", dirpath, entry->d_name);
                if (entry->d_type == DT_DIR) {
                    delete_directory(entrypath);  // Recursively delete subdirectories
                } else {
                    remove(entrypath);
                }
            }
        }
        closedir(d);
        rmdir(dirpath);
        printf("Deleted directory: %s\n", dirpath);
    } else {
        perror("Error deleting directory");
    }
}


void send_file_data(char *source_path, char* destination_path, int socket) {
    FILE *source = fopen(source_path, "r");
    if (!source) {
        perror("Error opening source file");
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    sprintf(buffer, "FILE %s", destination_path);
    send(socket, buffer, strlen(buffer), 0);
    memset(buffer, 0, BUFFER_SIZE);
    // Send the file content
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        printf("file buffer being sent: %s\n", buffer);
        send(socket, buffer, bytes_read, 0);
        memset(buffer, 0, BUFFER_SIZE);

    }
    strcpy(buffer, "EOF");
    send(socket, buffer, strlen(buffer), 0);
    fclose(source);
}


void copy_file_to_remote(char *source_path, char *destination_path, char *server_ip, int server_port) {
    // Create socket for communication
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Error creating socket");
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Connect to the destination server
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to destination server");
        close(socket_fd);
        return;
    }


    // Send the file data
    send_file_data(source_path, destination_path, socket_fd);

    printf("Copied file from %s to %s on server %s:%d\n", source_path, destination_path, server_ip, server_port);

    close(socket_fd);
}

void copy_directory_to_remote(char *source_path, char *destination_path, char *server_ip, int server_port) {
    char buffer[BUFFER_SIZE];
    // Create socket for communication
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Error creating socket");
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    printf("server port: %d\n", server_port);
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Connect to the destination server
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to destination server");
        close(socket_fd);
        return;
    }


    // Recursively send directory contents
    DIR *d = opendir(source_path);
    struct dirent *entry;
    printf("source path: %s\n", source_path);
    if (!d) {
        perror("Error opening source directory");
        close(socket_fd);
        return;
    }

    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char source_entry[DIR_SIZE + 280], dest_entry[DIR_SIZE + 280];
            snprintf(source_entry, sizeof(source_entry), "%s/%s", source_path, entry->d_name);
            snprintf(dest_entry, sizeof(dest_entry), "%s/%s", destination_path, entry->d_name);
            if (entry->d_type == DT_DIR) {
                sprintf(buffer, "DIR %s", dest_entry);
                if (send(socket_fd, buffer, strlen(buffer), 0) < 0) {
                    perror("Error sending file data");
                    return;
                }
                memset(buffer, 0, BUFFER_SIZE);
                copy_directory_to_remote(source_entry, dest_entry, server_ip, server_port);
            } 
            else {
                // Copy file to remote
                copy_file_to_remote(source_entry, dest_entry, server_ip, server_port);
            }
        }
    }

    closedir(d);
    printf("Copied directory from %s to %s on server %s:%d\n", source_path, destination_path, server_ip, server_port);

    close(socket_fd);
}




void receive_file(int client_socket, char *path) {
    // printf("path received in recv_file: %s\n", path);
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    FILE* file = fopen(path, "w");
    if (!file){
        printf("could not open file\n");
        exit(0);
    }
    while (1) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received < 0) {
            perror("Error receiving file data");
            break;
        }
        printf("file buffer: %s\n", buffer);
        // Check for EOF marker
        if (strcmp(buffer, "EOF") == 0) {
            printf("File transfer complete: %s\n", path);
            break;
        }

        fwrite(buffer, 1, bytes_received, file);
        // printf("written\n");
        memset(buffer, 0, BUFFER_SIZE);
    }

    fclose(file);
}
void receive_directory(int client_socket, char *path) {
    // printf("dir received in receive_dir: %s\n", path);
    if (mkdir(path, 0777) == 0) {
        printf("Directory created: %s\n", path);
    } else {
        perror("Error creating directory");
        return;
    }

}

// Thread to handle commands from the Naming Server
void *handle_naming_server_commands(void *arg) {
    int sockfd = *(int *)arg; // Cast the void* back to int*
    free(arg);  
    struct sockaddr_in nm_address;
    socklen_t len = sizeof(nm_address);
    int new_socket = accept(sockfd, (struct sockaddr *)&nm_address, &len);
    if (new_socket >= 0) {
        printf("Connection request from naming server accepted\n");
    }
    
    
    char buffer[BUFFER_SIZE];
    char path[DIR_SIZE];
    char source_path[DIR_SIZE];
    char dest_path[DIR_SIZE];
    char server_ip[INET_ADDRSTRLEN];
    char server_port[10];
    memset(path, 0, DIR_SIZE);
    memset(source_path, 0, DIR_SIZE);
    memset(dest_path, 0, DIR_SIZE);
    memset(server_ip, 0, INET_ADDRSTRLEN);
    memset(server_port, 0, 10);

    while (1) {
        // Receive command from Naming Server
        memset(buffer, 0, BUFFER_SIZE);
        memset(path, 0, DIR_SIZE);
        if (recv(new_socket, buffer, BUFFER_SIZE, 0) <= 0) {
            perror("Error receiving command");
            break;
        }
       
        pthread_mutex_lock(&file_mutex);

        if (strncmp(buffer, "CREATE_FILE", 11) == 0) {
            strcpy(path, buffer + 12);
            create_empty_file(path);
        } else if (strncmp(buffer, "DELETE_FILE", 11) == 0) {
            strcpy(path, buffer + 12);
            delete_file(path);
        }
         else if (strncmp(buffer, "DELETE_DIR", 10) == 0) {
            strcpy(path, buffer + 11);
            delete_directory(path);
        } 
        else if (strncmp(buffer, "COPY_FILE", 9) == 0) {
            char* token = strtok(buffer, " ");
            int i = 0;
            while (token != NULL) {
                if (i == 1){
                    strcpy(source_path, token);
                }
                else if(i == 2){
                    strcpy(dest_path, token);
                }
                else if(i == 3){
                    strcpy(server_ip, token);
                }
                else if(i == 4){
                    strcpy(server_port, token);
                }
                token = strtok(NULL, " ");
                i++;
            }
            ;
            copy_file_to_remote(source_path, dest_path, server_ip, atoi(server_port));
            memset(source_path, 0, DIR_SIZE);
            memset(dest_path, 0, DIR_SIZE);
            memset(server_ip, 0, INET_ADDRSTRLEN);
            memset(server_port, 0, 10);
        } 
        else if (strncmp(buffer, "COPY_DIR", 8) == 0) {
            // printf("msg received: %s\n", buffer);
            char* token = strtok(buffer, " ");
            int i = 0;
            while (token != NULL) {
                if (i == 1){
                    strcpy(source_path, token);
                }
                else if(i == 2){
                    strcpy(dest_path, token);
                }
                else if(i == 3){
                    strcpy(server_ip, token);
                }
                else if(i == 4){
                    strcpy(server_port, token);
                }
                token = strtok(NULL, " ");
                i++;
            }
            copy_directory_to_remote(source_path, dest_path, server_ip, atoi(server_port));
            memset(source_path, 0, DIR_SIZE);
            memset(dest_path, 0, DIR_SIZE);
            memset(server_ip, 0, INET_ADDRSTRLEN);
            memset(server_port, 0, 10);
        } else if (strncmp(buffer, "CREATE_DIR", 10) == 0) {
            strcpy(path, buffer + 11);
            create_directory(path);
        }
        else {
            printf("Unknown command received\n");
        }

        pthread_mutex_unlock(&file_mutex);
    }
    
    close(sockfd);
    return NULL;
}
void *connection_handler(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    char path[DIR_SIZE];
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        memset(path, 0, DIR_SIZE);
        // Receive metadata (command and path)
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        printf("%d bytes received\n", bytes_received);
        if (bytes_received < 0) {
            printf("Client disconnected\n");
            break;
        }

        buffer[bytes_received] = '\0';
        // printf("msg received by other ss: %s\n", buffer);
        

        if (strncmp(buffer, "FILE", 4) == 0) {
            strcpy(path, buffer + 5);
            receive_file(client_socket, path);
        } 
        else if (strncmp(buffer, "DIR", 3) == 0) {
            strcpy(path, buffer + 4);
            receive_directory(client_socket, path);
        } 
    }

    close(client_socket);
    return NULL;
}

void *connection_listener(void *arg) {
    int server_socket = *(int *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Error accepting connection");
            continue;
        }

        printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Spawn a new thread to handle the connection
        pthread_t client_thread;
        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socket;

        if (pthread_create(&client_thread, NULL, connection_handler, socket_ptr) != 0) {
            perror("Error creating thread");
            close(client_socket);
            free(socket_ptr);
        }
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    // Updated argv indices:
    // argv[1]: naming server port
    // argv[2]: naming server ip
    // argv[3]: client port
    // argv[4]: another port for receiving naming server commands
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <naming_server_port> <naming_server_ip> <client_port> <naming_server_listener_port>\n", argv[0]);
        return EXIT_FAILURE; 
    }

    StorageServer details;
    printf("Enter the paths that can be accessed from the storage server. When you're done, enter 'done': \n");
    details.num_paths = 0;
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Automatically retrieve the server's IP
    char* server_ip = get_server_ip();
    strncpy(details.ip_address, server_ip, sizeof(details.ip_address)-1);
    details.ip_address[sizeof(details.ip_address)-1] = '\0'; // Ensure null-termination

    // Parse other command-line arguments
    n_port = atoi(argv[1]);
    details.nm_port = n_port;
    strncpy(nm_server_ip, argv[2], sizeof(nm_server_ip)-1);
    nm_server_ip[sizeof(nm_server_ip)-1] = '\0';
    details.client_port = atoi(argv[3]);
    details.nm_listener_port = atoi(argv[4]);

    // Read accessible paths from user input
    while(1){
        if (!fgets(buffer, sizeof(buffer), stdin)){
            printf("Could not get path\n");
            return EXIT_FAILURE;
        }
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove trailing newline
        if(strcmp(buffer, "done") == 0){
            break;
        }
        strncpy(details.accessible_paths[details.num_paths++], buffer, sizeof(details.accessible_paths[0])-1);
        details.accessible_paths[details.num_paths-1][sizeof(details.accessible_paths[0])-1] = '\0'; // Ensure null-termination
        memset(buffer, 0, sizeof(buffer));
    }
    int nm_socket;
    struct sockaddr_in nm_addr;

    // Create socket for Naming Server communication
    nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_socket == -1) {
        perror("Could not create Naming Server socket");
        exit(EXIT_FAILURE);
    }

    // Configure Naming Server address
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_addr.s_addr = inet_addr(argv[2]);
    nm_addr.sin_port = htons(details.nm_port);
    if (inet_pton(AF_INET, argv[2], &nm_addr.sin_addr) <= 0) {
        perror("Invalid Naming Server address");
        close(nm_socket);
        exit(EXIT_FAILURE);
    }

    // Connect to Naming Server
    printf("Attempting to connect to Naming Server at %s:%d\n", argv[2], details.nm_port);
    if (connect(nm_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to Naming Server failed");
        close(nm_socket);
        exit(EXIT_FAILURE);
    }
    printf("Storage Server started on ports %d (client) and %d (naming server).\n", details.client_port, details.nm_port);
    if (send(nm_socket, &details, sizeof(StorageServer), 0) < 0) {
        perror("Failed to send details to Naming Server");
    } 
    else {
        printf("Storage Server details sent to Naming Server.\n");
    }

    
    
    int nm_listener_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_listener_socket < 0) {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in nm_listen_addr;
    nm_listen_addr.sin_family = AF_INET;
    nm_listen_addr.sin_addr.s_addr = inet_addr(server_ip);
    nm_listen_addr.sin_port = htons(details.nm_listener_port);
    if (bind(nm_listener_socket, (struct sockaddr *)&nm_listen_addr, sizeof(nm_listen_addr)) < 0) {
        perror("Error binding server socket");
        close(nm_listener_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(nm_listener_socket, 1) < 0) {
        perror("Error listening on server socket");
        close(nm_listener_socket);
        exit(EXIT_FAILURE);
    }
    pthread_t nm_listen_thread;
    int *socket_ptr_2 = malloc(sizeof(int));
    *socket_ptr_2 = nm_listener_socket;
    if (pthread_create(&nm_listen_thread, NULL, handle_naming_server_commands, socket_ptr_2) != 0) {
        perror("Failed to create listener thread");
        close(nm_listener_socket);
        exit(EXIT_FAILURE);
    }
    

    
    // Create socket for handling connections from other servers or clients
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(details.client_port);
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding server socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Error listening on server socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    pthread_t thread_listener;
    // Start the connection listener thread
    if (pthread_create(&thread_listener, NULL, connection_listener, &server_socket) != 0) {
        perror("Failed to create listener thread");
        close(server_socket);
        exit(EXIT_FAILURE);
    }


    pthread_join(nm_listen_thread, NULL);
    pthread_join(thread_listener, NULL);
    close(server_socket);
    return 0;
}



