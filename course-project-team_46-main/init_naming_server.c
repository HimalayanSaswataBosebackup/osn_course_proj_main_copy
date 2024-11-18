//init_naming_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "common.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netdb.h>
#define MAX_STORAGE_SERVERS 10
#define MAX_CLIENTS 50
#define BUFFER_SIZE 2048
#define MAX_DIRS 500
#define DIR_SIZE 1024
StorageServer storage_servers[MAX_STORAGE_SERVERS];
pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_args {
    int client_socket;
    struct sockaddr_in client_addr;
};

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

// Function to find appropriate storage server for a path
StorageServer* find_storage_server_for_path(const char *path) {
    pthread_mutex_lock(&servers_mutex);
    
    for (int i = 0; i < MAX_STORAGE_SERVERS; i++) {
        if (storage_servers[i].is_active) {
            for (int j = 0; j < storage_servers[i].num_paths; j++) {
                // Check if path starts with the accessible path
                if (strncmp(path, storage_servers[i].accessible_paths[j], 
                    strlen(storage_servers[i].accessible_paths[j])) == 0) {
                    pthread_mutex_unlock(&servers_mutex);
                    return &storage_servers[i];
                }
            }
        }
    }
    
    pthread_mutex_unlock(&servers_mutex);
    return NULL;
}

// Function to send command to storage server
void send_command_to_storage_server(StorageServer *server, const char *command) {
    // printf("entered\n");
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return;
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server->nm_port);
    server_addr.sin_addr.s_addr = inet_addr(server->ip_address);
    
    // Connect to storage server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to storage server failed");
        close(sockfd);
        return;
    }
    
    // Send command
    if (send(sockfd, command, strlen(command), 0) < 0) {
        perror("Failed to send command");
    }

    
    close(sockfd);
}

// Operation handlers
void handle_create_file(const char *filepath) {
    StorageServer *server = find_storage_server_for_path(filepath);
    if (server) {
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "CREATE_FILE %s", filepath);
        send_command_to_storage_server(server, command);
    } else {
        printf("No storage server found for path: %s\n", filepath);
    }
}

void handle_delete_file(const char *filepath) {
    StorageServer *server = find_storage_server_for_path(filepath);
    if (server) {
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "DELETE_FILE %s", filepath);
        send_command_to_storage_server(server, command);
    } else {
        printf("No storage server found for path: %s\n", filepath);
    }
}

void handle_create_directory(const char *dirpath) {
    StorageServer *server = find_storage_server_for_path(dirpath);
    if (server) {
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "CREATE_DIR %s", dirpath);
        send_command_to_storage_server(server, command);
    } else {
        printf("No storage server found for path: %s\n", dirpath);
    }
}

void handle_delete_directory(const char *dirpath) {
    StorageServer *server = find_storage_server_for_path(dirpath);
    if (server) {
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "DELETE_DIR %s", dirpath);
        send_command_to_storage_server(server, command);
    } else {
        printf("No storage server found for path: %s\n", dirpath);
    }
}

void handle_copy_file(const char *source_path, const char *dest_path) {
    StorageServer *source_server = find_storage_server_for_path(source_path);
    StorageServer *dest_server = find_storage_server_for_path(dest_path);
    
    if (source_server && dest_server) {
        char command[BUFFER_SIZE];
        // Send copy command to source server with destination server details
        snprintf(command, sizeof(command), "COPY_FILE %s %s %s %d", 
                source_path, dest_path, dest_server->ip_address, dest_server->nm_port); //after setting up client, change to dest_Server->client_port
        send_command_to_storage_server(source_server, command);
    } else {
        printf("Source or destination server not found\n");
    }
}

void handle_copy_directory(const char *source_path, const char *dest_path) {
    StorageServer *source_server = find_storage_server_for_path(source_path);
    StorageServer *dest_server = find_storage_server_for_path(dest_path);
    
    if (source_server && dest_server) {
        char command[BUFFER_SIZE];
        // printf("source server ip: %s\n", source_server->ip_address);
        // printf("dest server ip: %s\n", dest_server->ip_address);
        // Send copy command to source server with destination server details
        snprintf(command, sizeof(command), "COPY_DIR %s %s %s %d", 
                source_path, dest_path, dest_server->ip_address, dest_server->nm_port); //after setting up client, change to dest_Server->client_port
        send_command_to_storage_server(source_server, command);
    } 
    else {
        printf("Source or destination server not found\n");
    }
}

// Main client handler function
// void *handle_client(void *socket_desc) {
//     int client_socket = *(int*)socket_desc;
//     char buffer[BUFFER_SIZE];
//     free(socket_desc);

//     while (1) {
//         memset(buffer, 0, BUFFER_SIZE);
//         int read_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
//         if (read_size <= 0) {
//             break;
//         }
//         buffer[read_size] = '\0';

//         // Parse command
//         char command[20], path1[256], path2[256];
//         int params = sscanf(buffer, "%s %s %s", command, path1, path2);

//         if (params < 2) {
//             send(client_socket, "Invalid command format", 21, 0);
//             continue;
//         }

//         // Handle different commands
//         if (strcmp(command, "CREATE_FILE") == 0) {
//             handle_create_file(path1);
//         }
//         else if (strcmp(command, "DELETE_FILE") == 0) {
//             handle_delete_file(path1);
//         }
//         else if (strcmp(command, "CREATE_DIR") == 0) {
//             handle_create_directory(path1);
//         }
//         else if (strcmp(command, "DELETE_DIR") == 0) {
//             handle_delete_directory(path1);
//         }
//         else if (strcmp(command, "COPY_FILE") == 0 && params == 3) {
//             handle_copy_file(path1, path2);
//         }
//         else if (strcmp(command, "COPY_DIR") == 0 && params == 3) {
//             handle_copy_directory(path1, path2);
//         }
//         else {
//             send(client_socket, "Unknown command", 14, 0);
//         }
//     }

//     close(client_socket);
//     return NULL;
// }

// Function to handle new storage server registration
void* handle_storage_server_registration(void* args) {
    struct thread_args *thread_data = (struct thread_args *)args;
    int socket = thread_data->client_socket;
    struct sockaddr_in client_addr = thread_data->client_addr;
    StorageServer new_server;
    memset(&new_server, 0, sizeof(StorageServer)); // Initialize to zero
    int total_bytes_received = 0;
    while (total_bytes_received < sizeof(StorageServer)) {
        int bytes = recv(socket, ((char *)&new_server) + total_bytes_received, sizeof(StorageServer) - total_bytes_received, 0);
        if (bytes < 0) {
            perror("Failed to receive structure");
            break;
        }
        total_bytes_received += bytes;
    }

    pthread_mutex_lock(&servers_mutex);
    // printf("num paths: %d\n", new_server.num_paths);
    // Check for duplicate servers or find empty slot
    int server_index = -1;
    for (int i = 0; i < MAX_STORAGE_SERVERS; i++) {
        if (!storage_servers[i].is_active) {
            server_index = i;
            break;
        }
    }

    if (server_index == -1) {
        printf("Maximum number of storage servers reached\n");
        pthread_mutex_unlock(&servers_mutex);
        return NULL;
    }

    // Store new server details
    storage_servers[server_index] = new_server;
    storage_servers[server_index].is_active = 1;
    storage_servers[server_index].last_heartbeat = time(NULL);
    pthread_mutex_lock(&num_ss_mutex);
    num_storage_servers++;
    pthread_mutex_unlock(&num_ss_mutex);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    printf("Accepted connection from %s:%d\n", client_ip, ntohs(client_addr.sin_port));
    printf("Storage server connected for registration.\n");

    printf("New storage server registered:\n");
    printf("IP: %s\n", new_server.ip_address);
    printf("NM Port: %d\n", new_server.nm_port);
    printf("Client Port: %d\n", new_server.client_port);
    printf("Accessible paths:\n");
    for (int i = 0; i < new_server.num_paths; i++) {
        printf("- %s\n", new_server.accessible_paths[i]);
    }

    pthread_mutex_unlock(&servers_mutex);
    return NULL;
}

void *storage_server_listener(void *arg) {
    int registration_socket = *(int *)arg;
    int n_ss;
    pthread_mutex_lock(&num_ss_mutex);
    n_ss = num_storage_servers;
    pthread_mutex_unlock(&num_ss_mutex);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        if(n_ss < MAX_STORAGE_SERVERS){
            int new_socket = accept(registration_socket, (struct sockaddr *)&client_addr, &client_len);
            if (new_socket < 0) {
                perror("Accept failed for storage server");
                continue;
            }
            
            struct thread_args thread_data;
            thread_data.client_socket = new_socket;
            thread_data.client_addr = client_addr;
            
            pthread_t storage_server_thread;
            if (pthread_create(&storage_server_thread, NULL, handle_storage_server_registration,(void*)&thread_data) != 0) {
                perror("Failed to create thread for client");
                close(new_socket);
                continue;
            }

            pthread_detach(storage_server_thread);
        }
    }
    return NULL;
}
// Function to check storage server health periodically
void *monitor_storage_servers(void *arg) {
    while (1) {
        pthread_mutex_lock(&servers_mutex);
        
        time_t current_time = time(NULL);
        for (int i = 0; i < MAX_STORAGE_SERVERS; i++) {
            if (storage_servers[i].is_active) {
                // If no heartbeat received in last 30 seconds, mark server as inactive
                if (difftime(current_time, storage_servers[i].last_heartbeat) > 30) {
                    printf("Storage server %s:%d marked as inactive\n", 
                           storage_servers[i].ip_address, 
                           storage_servers[i].nm_port);
                    storage_servers[i].is_active = 0;
                    pthread_mutex_lock(&num_ss_mutex);
                    num_storage_servers--;
                    pthread_mutex_unlock(&num_ss_mutex);
                }
            }
        }
        
        pthread_mutex_unlock(&servers_mutex);
        sleep(10); // Check every 10 seconds
    }
    return NULL;
}

// Function to handle client requests
// void *handle_client(void *socket_desc) {
//     int client_socket = *(int*)socket_desc;
//     char buffer[BUFFER_SIZE];
//     free(socket_desc);

//     while (1) {
//         int read_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
//         if (read_size <= 0) {
//             break;
//         }
//         buffer[read_size] = '\0';

//         // Process client request and respond with appropriate storage server details
//         // This is where you would implement the logic to handle different client commands
//         // and return the appropriate storage server information
//     }

//     close(client_socket);
//     return NULL;
// }

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Could not create socket");
        return 1;
    }

    // Set up server address structure
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    printf("Naming Server started on %s:%d\n", get_server_ip(), port);
    listen(server_socket, 5);

    // pthread_t monitor_thread;
    // if (pthread_create(&monitor_thread, NULL, monitor_storage_servers, NULL) < 0) {
    //     perror("Could not create monitoring thread");
    //     return 1;
    // }

    int *socket_desc = malloc(sizeof(int));
    *socket_desc = server_socket;
    pthread_t registration_thread;
    if (pthread_create(&registration_thread, NULL, storage_server_listener, (void *)socket_desc) < 0) {
        perror("Could not create registration listener thread");
        close(server_socket);
        return 1;
    }
    
    // Start storage server monitoring thread
   
    // printf("Now sending test dir. Waiting for 30 seconds\n");
    // sleep(10);
    // printf("20 seconds\n");
    // sleep(10);
    // printf("10 seconds\n");
    // sleep(5);
    // printf("5 seconds\n");
    // sleep(5);
    // printf("time up\n");
    // handle_copy_directory("anothaone", "toto");


    // Accept and handle connections
    // struct sockaddr_in client_addr;
    // socklen_t client_len = sizeof(client_addr);
    
    // while (1) {
    //     int *new_socket = malloc(sizeof(int));
    //     *new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        
    //     if (*new_socket < 0) {
    //         perror("Accept failed");
    //         free(new_socket);
    //         continue;
    //     }

    //     // Create new thread to handle the connection
    //     pthread_t client_thread;
    //     if (pthread_create(&client_thread, NULL, handle_client, (void*)new_socket) < 0) {
    //         perror("Could not create thread");
    //         free(new_socket);
    //         continue;
    //     }
    // }
    pthread_join(registration_thread, NULL);
    // pthread_join(monitor_thread, NULL);
    close(server_socket);
    return 0;
}    
