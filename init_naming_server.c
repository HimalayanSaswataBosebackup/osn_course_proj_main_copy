
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
#include <stdbool.h>
#include <netdb.h>
#define MAX_STORAGE_SERVERS 10
#define MAX_CLIENTS 50
#define BUFFER_SIZE 2048
#define MAX_DIRS 500
#define DIR_SIZE 1024
#define MAX_PATH_LENGTH 256
#define LRU_CACHE_SIZE 100

#define INITIAL_ACK "INITIAL_ACK"
#define FINAL_ACK "FINAL_ACK"
#define ACK_TIMEOUT 5

StorageServer storage_servers[MAX_STORAGE_SERVERS];
pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_args {
    int client_socket;
    struct sockaddr_in client_addr;
};

typedef struct {
    char filepath[256];
    int writers;
    int readers;
    pthread_mutex_t lock;
    pthread_cond_t read_cond;
    pthread_cond_t write_cond;
} FileAccess;

typedef struct TrieNode {
    struct TrieNode* children[256];  // One for each possible character
    StorageServer* server;           // Associated storage server
    bool isEndOfPath;               // Marks end of a registered path
} TrieNode;

typedef struct LRUNode {
    char* path;
    StorageServer* server;
    struct LRUNode *prev, *next;
} LRUNode;

typedef struct {
    int capacity;
    int size;
    LRUNode* head;
    LRUNode* tail;
    LRUNode** hashmap;  // Simple hashmap for O(1) lookup
    pthread_mutex_t cache_mutex;
} LRUCache;

FileAccess file_accesses[MAX_CLIENTS];
int num_files = 0;
pthread_mutex_t files_mutex = PTHREAD_MUTEX_INITIALIZER;

StorageServer storage_servers[MAX_STORAGE_SERVERS];
// pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;
TrieNode* root;
LRUCache* cache;

FileAccess* get_file_access(char* filepath) {
    pthread_mutex_lock(&files_mutex);
    for (int i = 0; i < num_files; i++) {
        if (strcmp(file_accesses[i].filepath, filepath) == 0) {
            pthread_mutex_unlock(&files_mutex);
            return &file_accesses[i];
        }
    }
    
    if (num_files < MAX_CLIENTS) {
        FileAccess* fa = &file_accesses[num_files++];
        strncpy(fa->filepath, filepath, sizeof(fa->filepath) - 1);
        fa->writers = 0;
        fa->readers = 0;
        pthread_mutex_init(&fa->lock, NULL);
        pthread_cond_init(&fa->read_cond, NULL);
        pthread_cond_init(&fa->write_cond, NULL);
        pthread_mutex_unlock(&files_mutex);
        return fa;
    }
    
    pthread_mutex_unlock(&files_mutex);
    return NULL;
}

void acquire_read_lock(FileAccess* fa) {
    pthread_mutex_lock(&fa->lock);
    while (fa->writers > 0) {
        pthread_cond_wait(&fa->read_cond, &fa->lock);
    }
    fa->readers++;
    pthread_mutex_unlock(&fa->lock);
}

void release_read_lock(FileAccess* fa) {
    pthread_mutex_lock(&fa->lock);
    fa->readers--;
    if (fa->readers == 0) {
        pthread_cond_signal(&fa->write_cond);
    }
    pthread_mutex_unlock(&fa->lock);
}

void acquire_write_lock(FileAccess* fa) {
    pthread_mutex_lock(&fa->lock);
    while (fa->writers > 0 || fa->readers > 0) {
        pthread_cond_wait(&fa->write_cond, &fa->lock);
    }
    fa->writers = 1;
    pthread_mutex_unlock(&fa->lock);
}

void release_write_lock(FileAccess* fa) {
    pthread_mutex_lock(&fa->lock);
    fa->writers = 0;
    pthread_cond_broadcast(&fa->read_cond);
    pthread_cond_signal(&fa->write_cond);
    pthread_mutex_unlock(&fa->lock);
}

TrieNode* createTrieNode() {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    memset(node->children, 0, sizeof(node->children));
    node->server = NULL;
    node->isEndOfPath = false;
    return node;
}

LRUCache* initLRUCache(int capacity) {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    cache->hashmap = (LRUNode**)calloc(capacity, sizeof(LRUNode*));
    pthread_mutex_init(&cache->cache_mutex, NULL);
    printf("cache done\n");
    return cache;
}

unsigned int hashFunction(const char* path, int capacity) {
    unsigned int hash = 0;
    for (int i = 0; path[i] != '\0'; i++) {
        hash = (hash * 31 + path[i]) % capacity;
    }
    return hash;
}

void addPathToTrie(const char* path, StorageServer* server) {
    TrieNode* current = root;
    
    for (int i = 0; path[i] != '\0'; i++) {
        unsigned char index = (unsigned char)path[i];
        if (current->children[index] == NULL) {
            current->children[index] = createTrieNode();
        }
        current = current->children[index];
    }
    printf("path added to trie\n");
    current->isEndOfPath = true;
    current->server = server;
    printf("path added to trie --- 2\n");
}

// Function to find longest matching prefix in Trie
StorageServer* findInTrie(const char* path) {
    TrieNode* current = root;
    TrieNode* lastMatch = NULL;
    
    for (int i = 0; path[i] != '\0' && current != NULL; i++) {
        unsigned char index = (unsigned char)path[i];
        if (current->isEndOfPath) {
            lastMatch = current;
        }
        current = current->children[index];
    }
    printf("path found in trie\n");
    
    return lastMatch ? lastMatch->server : NULL;
}

void addToCache(const char* path, StorageServer* server) {
    pthread_mutex_lock(&cache->cache_mutex);
    
    // Create new node
    LRUNode* newNode = (LRUNode*)malloc(sizeof(LRUNode));
    newNode->path = strdup(path);
    newNode->server = server;

    printf("new node\n");
    
    // Add to hashmap
    unsigned int hash = hashFunction(path, cache->capacity);
    cache->hashmap[hash] = newNode;
    
    // Add to front of linked list
    newNode->next = cache->head;
    newNode->prev = NULL;
    if (cache->head) {
        cache->head->prev = newNode;
    }
    cache->head = newNode;
    
    if (!cache->tail) {
        cache->tail = newNode;
    }
    printf("added to cache\n");
    // Handle capacity
    if (cache->size >= cache->capacity) {
        // Remove from hashmap
        unsigned int tailHash = hashFunction(cache->tail->path, cache->capacity);
        cache->hashmap[tailHash] = NULL;
        
        // Remove from linked list
        LRUNode* temp = cache->tail;
        cache->tail = cache->tail->prev;
        cache->tail->next = NULL;
        free(temp->path);
        free(temp);
        printf("removed from cache\n");
    } else {
        cache->size++;
        printf("size increased\n");
    }
    
    pthread_mutex_unlock(&cache->cache_mutex);
}

StorageServer* getFromCache(const char* path) {
    pthread_mutex_lock(&cache->cache_mutex);
    
    unsigned int hash = hashFunction(path, cache->capacity);
    LRUNode* node = cache->hashmap[hash];
    printf("got from cache\n");
    
    if (node == NULL) {
        pthread_mutex_unlock(&cache->cache_mutex);
        return NULL;
    }
    
    // Move to front (most recently used)
    if (node != cache->head) {
        // Remove from current position
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        if (node == cache->tail) cache->tail = node->prev;
        printf("moved to front\n");
        
        // Move to front
        node->next = cache->head;
        node->prev = NULL;
        cache->head->prev = node;
        cache->head = node;
        printf("moved to front --- 2\n");
    }
    
    StorageServer* result = node->server;
    pthread_mutex_unlock(&cache->cache_mutex);
    return result;
}



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

// // Function to find appropriate storage server for a path
// StorageServer* old_find_storage_server_for_path(const char *path) {
//     pthread_mutex_lock(&servers_mutex);
    
//     for (int i = 0; i < MAX_STORAGE_SERVERS; i++) {
//         if (storage_servers[i].is_active) {
//             for (int j = 0; j < storage_servers[i].num_paths; j++) {
//                 // Check if path starts with the accessible path
//                 if (strncmp(path, storage_servers[i].accessible_paths[j], 
//                     strlen(storage_servers[i].accessible_paths[j])) == 0) {
//                     pthread_mutex_unlock(&servers_mutex);
//                     return &storage_servers[i];
//                 }
//             }
//         }
//     }
    
//     pthread_mutex_unlock(&servers_mutex);
//     return NULL;
// }

StorageServer* find_storage_server_for_path(const char* path) {
    // First check LRU cache
    StorageServer* cachedServer = getFromCache(path);
    if (cachedServer != NULL) {
        return cachedServer;
    }
    
    // If not in cache, search in Trie
    pthread_mutex_lock(&servers_mutex);
    StorageServer* server = findInTrie(path);
    pthread_mutex_unlock(&servers_mutex);
    
    // Add to cache if found
    if (server != NULL) {
        addToCache(path, server);
    }
    
    return server;
}

// Function to send command to storage server
void send_command_to_storage_server(StorageServer *server, const char *command) {
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
        // Send copy command to source server with destination server details
        snprintf(command, sizeof(command), "COPY_DIR %s %s %s %d", 
                source_path, dest_path, dest_server->ip_address, dest_server->nm_port); //after setting up client, change to dest_Server->client_port
        send_command_to_storage_server(source_server, command);
    } else {
        printf("Source or destination server not found\n");
    }
}

void* handle_storage_server_registration(void* args) {
    struct thread_args *thread_data = (struct thread_args *)args;
    int socket = thread_data->client_socket;
    struct sockaddr_in client_addr = thread_data->client_addr;
    StorageServer new_server;
    memset(&new_server, 0, sizeof(StorageServer)); // Initialize to zero
    
    int total_bytes_received = 0;
    while (total_bytes_received < sizeof(StorageServer)) {
        int bytes = recv(socket, ((char *)&new_server) + total_bytes_received, 
                        sizeof(StorageServer) - total_bytes_received, 0);
        if (bytes < 0) {
            perror("Failed to receive structure");
            break;
        }
        total_bytes_received += bytes;
    }

    pthread_mutex_lock(&servers_mutex);
    
    // Check for duplicate servers or find empty slot
    int server_index = -1;
    for (int i = 0; i < MAX_STORAGE_SERVERS; i++) {
        if (!storage_servers[i].is_active) {
            server_index = i;
            break;
        }
    }
    printf("working\n");

    if (server_index == -1) {
        printf("Maximum number of storage servers reached\n");
        pthread_mutex_unlock(&servers_mutex);
        return NULL;
    }

    // Store new server details
    storage_servers[server_index] = new_server;
    storage_servers[server_index].is_active = 1;
    storage_servers[server_index].last_heartbeat = time(NULL);

    // Add all accessible paths to the Trie
    for (int i = 0; i < new_server.num_paths; i++) {
        addPathToTrie(new_server.accessible_paths[i], &storage_servers[server_index]);
        printf("Added path %s to trie\n", new_server.accessible_paths[i]);
    }

    pthread_mutex_lock(&num_ss_mutex);
    num_storage_servers++;
    pthread_mutex_unlock(&num_ss_mutex);

    // Log connection details
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

// void *handle_client(void *socket_desc) {
//     int client_socket = *(int*)socket_desc;
//     char buffer[BUFFER_SIZE];
//     free(socket_desc);

//     // Send initial acknowledgment
//     send(client_socket, INITIAL_ACK, strlen(INITIAL_ACK), 0);

//     while (1) {
//         memset(buffer, 0, BUFFER_SIZE);
//         int read_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
//         if (read_size <= 0) break;
        
//         buffer[read_size] = '\0';
//         char command[20], path1[256], path2[256];
//         int params = sscanf(buffer, "%s %s %s", command, path1, path2);

//         if (params < 2) {
//             send(client_socket, "Invalid command format", 21, 0);
//             continue;
//         }

//         // Get file access control structure for operations that need it
//         FileAccess* fa = NULL;
//         if (strcmp(command, "READ") == 0) {
//             fa = get_file_access(path1);
//             if (fa) acquire_read_lock(fa);
//         } else if (strcmp(command, "WRITE") == 0 || strcmp(command, "CREATE_FILE") == 0) {
//             fa = get_file_access(path1);
//             if (fa) acquire_write_lock(fa);
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

//         // Release locks if they were acquired
//         if (fa) {
//             if (strcmp(command, "READ") == 0) {
//                 release_read_lock(fa);
//             } else if (strcmp(command, "WRITE") == 0 || strcmp(command, "CREATE_FILE") == 0) {
//                 release_write_lock(fa);
//             }
//         }

//         // Send final acknowledgment
//         send(client_socket, FINAL_ACK, strlen(FINAL_ACK), 0);
//     }

//     close(client_socket);
//     return NULL;
// }

// Function to handle new storage server registration
// void* old_handle_storage_server_registration(void* args) {
//     struct thread_args *thread_data = (struct thread_args *)args;
//     int socket = thread_data->client_socket;
//     struct sockaddr_in client_addr = thread_data->client_addr;
//     StorageServer new_server;
//     memset(&new_server, 0, sizeof(StorageServer)); // Initialize to zero
//     int total_bytes_received = 0;
//     while (total_bytes_received < sizeof(StorageServer)) {
//         int bytes = recv(socket, ((char *)&new_server) + total_bytes_received, sizeof(StorageServer) - total_bytes_received, 0);
//         if (bytes < 0) {
//             perror("Failed to receive structure");
//             break;
//         }
//         total_bytes_received += bytes;
//     }

//     pthread_mutex_lock(&servers_mutex);
//     // printf("num paths: %d\n", new_server.num_paths);
//     // Check for duplicate servers or find empty slot
//     int server_index = -1;
//     for (int i = 0; i < MAX_STORAGE_SERVERS; i++) {
//         if (!storage_servers[i].is_active) {
//             server_index = i;
//             break;
//         }
//     }

//     if (server_index == -1) {
//         printf("Maximum number of storage servers reached\n");
//         pthread_mutex_unlock(&servers_mutex);
//         return NULL;
//     }

//     // Store new server details
//     storage_servers[server_index] = new_server;
//     storage_servers[server_index].is_active = 1;
//     storage_servers[server_index].last_heartbeat = time(NULL);
//     pthread_mutex_lock(&num_ss_mutex);
//     num_storage_servers++;
//     pthread_mutex_unlock(&num_ss_mutex);

//     char client_ip[INET_ADDRSTRLEN];
//     inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
//     printf("Accepted connection from %s:%d\n", client_ip, ntohs(client_addr.sin_port));
//     printf("Storage server connected for registration.\n");

//     printf("New storage server registered:\n");
//     printf("IP: %s\n", new_server.ip_address);
//     printf("NM Port: %d\n", new_server.nm_port);
//     printf("Client Port: %d\n", new_server.client_port);
//     printf("Accessible paths:\n");
//     for (int i = 0; i < new_server.num_paths; i++) {
//         printf("- %s\n", new_server.accessible_paths[i]);
//     }

//     pthread_mutex_unlock(&servers_mutex);
//     return NULL;
// }


// void* storage_server_listener(void* storage_socket) {
//     int socket = *(int*)storage_socket;
//     free(storage_socket);

//     while (1) {
//         struct sockaddr_in storage_conn_addr;
//         socklen_t storage_len = sizeof(storage_conn_addr);

//         int new_socket = accept(socket, (struct sockaddr *)&storage_conn_addr, &storage_len);

//         if (new_socket < 0) {
//             perror("Accept failed for storage server");
//             continue;
//         }

//         printf("New storage server connected from %s:%d\n",
//                inet_ntoa(storage_conn_addr.sin_addr),
//                ntohs(storage_conn_addr.sin_port));

//         // Here you would handle the storage server registration
//         // For now, we just print a message and close the connection
//         printf("Handling storage server registration...\n");
//         close(new_socket);
//     }

//     return NULL;
// }

void *storage_server_listener(void *arg) {
    // printf("Call ho rha\n");
    int registration_socket = *(int *)arg;
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        if(num_storage_servers < MAX_STORAGE_SERVERS){
            int new_socket = accept(registration_socket, (struct sockaddr *)&client_addr, &client_len);
            if (new_socket < 0) {
                perror("Accept failed for storage server");
                continue;
            }
            else{
                printf("Accept passed for SS\n");
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
                    num_storage_servers--;
                }
            }
        }
        
        pthread_mutex_unlock(&servers_mutex);
        sleep(10); // Check every 10 seconds
    }
    return NULL;
}

void *handle_client(void *socket_desc) {
    int client_socket = *(int*)socket_desc;
    char buffer[BUFFER_SIZE];
    free(socket_desc);

    // Send initial acknowledgment
    send(client_socket, INITIAL_ACK, strlen(INITIAL_ACK), 0);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int read_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (read_size <= 0) break;
        
        buffer[read_size] = '\0';
        char command[20], path1[256], path2[256];
        int params = sscanf(buffer, "%s %s %s", command, path1, path2);

        if (params < 2) {
            send(client_socket, "Invalid command format", 21, 0);
            continue;
        }
        else{
            printf("Command: %s\n", command);
            printf("Path 1: %s\n", path1);
            printf("Path 2: %s\n", path2);
        }

        // Get file access control structure for operations that need it
        FileAccess* fa = NULL;
        if (strcmp(command, "READ") == 0) {
            fa = get_file_access(path1);
            if (fa) acquire_read_lock(fa);
        } else if (strcmp(command, "WRITE") == 0 || strcmp(command, "CREATE_FILE") == 0) {
            fa = get_file_access(path1);
            if (fa) acquire_write_lock(fa);
        }

        // Handle different commands
        if (strcmp(command, "CREATE") == 0) {
            handle_create_file(path1);
        }
        else if (strcmp(command, "DELETE") == 0) {
            handle_delete_file(path1);
        }
        else if (strcmp(command, "CREATE_DIR") == 0) {
            handle_create_directory(path1);
        }
        else if (strcmp(command, "DELETE_DIR") == 0) {
            handle_delete_directory(path1);
        }
        else if (strcmp(command, "COPY") == 0 && params == 3) {
            handle_copy_file(path1, path2);
        }
        else if (strcmp(command, "COPY_DIR") == 0 && params == 3) {
            handle_copy_directory(path1, path2);
        }
        else {
            send(client_socket, "Unknown command", 14, 0);
        }

        // Release locks if they were acquired
        if (fa) {
            if (strcmp(command, "READ") == 0) {
                release_read_lock(fa);
            } else if (strcmp(command, "WRITE") == 0 || strcmp(command, "CREATE_FILE") == 0) {
                release_write_lock(fa);
            }
        }

        // Send final acknowledgment
        send(client_socket, FINAL_ACK, strlen(FINAL_ACK), 0);
    }

    close(client_socket);
    return NULL;
}


// int main(int argc, char *argv[]) {
//     if (argc != 2) {
//         printf("Usage: %s <port>\n", argv[0]);
//         return 1;
//     }

//     int port = atoi(argv[1]);

//     // Initialize Trie and LRU Cache (assume implementations are available)
//     root = createTrieNode();
//     cache = initLRUCache(LRU_CACHE_SIZE);

//     // Create socket for client connections
//     int client_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (client_socket == -1) {
//         perror("Could not create client socket");
//         return 1;
//     }

//     // Create socket for storage server connections
//     int storage_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (storage_socket == -1) {
//         perror("Could not create storage server socket");
//         close(client_socket);
//         return 1;
//     }

//     // Set up client address structure
//     struct sockaddr_in client_addr;
//     client_addr.sin_family = AF_INET;
//     client_addr.sin_addr.s_addr = INADDR_ANY;
//     client_addr.sin_port = htons(port);

//     // Set up storage server address structure
//     struct sockaddr_in storage_addr;
//     storage_addr.sin_family = AF_INET;
//     storage_addr.sin_addr.s_addr = INADDR_ANY;
//     storage_addr.sin_port = htons(port + STORAGE_SERVER_PORT_OFFSET);

//     // Bind client socket
//     if (bind(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
//         perror("Bind failed for client socket");
//         close(client_socket);
//         close(storage_socket);
//         return 1;
//     }

//     // Bind storage server socket
//     if (bind(storage_socket, (struct sockaddr *)&storage_addr, sizeof(storage_addr)) < 0) {
//         perror("Bind failed for storage server socket");
//         close(client_socket);
//         close(storage_socket);
//         return 1;
//     }

//     // Start listening on both sockets
//     if (listen(client_socket, BACKLOG) < 0) {
//         perror("Listen failed for client socket");
//         close(client_socket);
//         close(storage_socket);
//         return 1;
//     }

//     if (listen(storage_socket, BACKLOG) < 0) {
//         perror("Listen failed for storage server socket");
//         close(client_socket);
//         close(storage_socket);
//         return 1;
//     }

//     printf("Naming Server started on %s:%d\n", get_server_ip(), port);
//     printf("Storage Server listener started on %s:%d\n", get_server_ip(), port + STORAGE_SERVER_PORT_OFFSET);

//     // Create a thread to handle storage server connections
//     pthread_t storage_thread;
//     int *storage_socket_ptr = malloc(sizeof(int));
//     *storage_socket_ptr = storage_socket;

//     if (pthread_create(&storage_thread, NULL, storage_server_listener, (void*)storage_socket_ptr) < 0) {
//         perror("Could not create storage server listener thread");
//         close(client_socket);
//         close(storage_socket);
//         free(storage_socket_ptr);
//         return 1;
//     }

//     // Main loop for handling client connections
//     while (1) {
//         struct sockaddr_in client_conn_addr;
//         socklen_t client_len = sizeof(client_conn_addr);

//         int *new_socket = malloc(sizeof(int));
//         *new_socket = accept(client_socket, (struct sockaddr *)&client_conn_addr, &client_len);

//         if (*new_socket < 0) {
//             perror("Accept failed for client connection");
//             free(new_socket);
//             continue;
//         }

//         printf("New client connected from %s:%d\n",
//                inet_ntoa(client_conn_addr.sin_addr),
//                ntohs(client_conn_addr.sin_port));

//         pthread_t client_thread;
//         if (pthread_create(&client_thread, NULL, handle_client, (void*)new_socket) < 0) {
//             perror("Could not create thread for client");
//             free(new_socket);
//             continue;
//         }

//         pthread_detach(client_thread);
//     }

//     // Wait for storage server listener thread to finish
//     pthread_join(storage_thread, NULL);

//     close(client_socket);
//     close(storage_socket);

//     return 0;
// }

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    // Initialize Trie
    root = createTrieNode();
    
    // Initialize LRU cache
    cache = initLRUCache(LRU_CACHE_SIZE);

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

        while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (*new_socket < 0) {
            perror("Accept failed");
            free(new_socket);
            continue;
        }
        else{
            printf("New Socket: %d\n", *new_socket);
        }

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, (void*)new_socket) < 0) {
            perror("Could not create thread");
            free(new_socket);
            continue;
        }
        printf("Client Handled\n");
        printf("Detach\n");
        pthread_detach(client_thread);
        printf("Done\n");

    printf("Malloc\n");
    int *socket_desc = malloc(sizeof(int));
    *socket_desc = server_socket;
    pthread_t registration_thread;
    if (pthread_create(&registration_thread, NULL, storage_server_listener, (void *)socket_desc) < 0) {
        perror("Could not create registration listener thread");
        close(server_socket);
        return 1;
    }
    
    pthread_join(registration_thread, NULL);
    // pthread_join(monitor_thread, NULL);
    close(server_socket);
    }
    return 0;
}    


/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

// Function prototypes
void* handle_client(void* client_socket);
char* get_server_ip();

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
        close(server_socket);
        return 1;
    }

    printf("Naming Server started on %s:%d\n", get_server_ip(), port);
    listen(server_socket, 5);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (*new_socket < 0) {
            perror("Accept failed");
            free(new_socket);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("New connection accepted from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, (void*)new_socket) < 0) {
            perror("Could not create thread");
            free(new_socket);
            continue;
        }

        pthread_detach(client_thread);
    }

    close(server_socket);
    return 0;
}

// Handle client interactions
void* handle_client(void* client_socket) {
    int socket = *(int*)client_socket;
    free(client_socket);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // Acknowledge the connection
    const char* welcome_message = "Welcome to the Naming Server!";
    send(socket, welcome_message, strlen(welcome_message), 0);

    while ((bytes_received = recv(socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("Received from client: %s\n", buffer);

        // Respond to client command
        if (strcasecmp(buffer, "STATUS") == 0) {
            const char* status_message = "Naming Server is running.";
            send(socket, status_message, strlen(status_message), 0);
        } else {
            const char* unknown_command = "Unknown command.";
            send(socket, unknown_command, strlen(unknown_command), 0);
        }
    }

    if (bytes_received == 0) {
        printf("Client disconnected.\n");
    } else {
        perror("recv failed");
    }

    close(socket);
    return NULL;
}

// Utility function to get the server's IP address
char* get_server_ip() {
    static char ip[INET_ADDRSTRLEN] = "127.0.0.1"; // Default localhost
    // In a real scenario, replace this with code to get the actual IP
    return ip;
}

*/