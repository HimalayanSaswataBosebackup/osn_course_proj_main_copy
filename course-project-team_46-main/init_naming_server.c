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
StorageServer storage_servers[MAX_STORAGE_SERVERS];
pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_args {
    int client_socket;
    struct sockaddr_in client_addr;
};

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

StorageServer storage_servers[MAX_STORAGE_SERVERS];
// pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;
TrieNode* root;
LRUCache* cache;

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
    printf("cache done");
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
    printf("path added to trie");
    current->isEndOfPath = true;
    current->server = server;
    printf("path added to trie --- 2");
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
    printf("path found in trie");
    
    return lastMatch ? lastMatch->server : NULL;
}

void addToCache(const char* path, StorageServer* server) {
    pthread_mutex_lock(&cache->cache_mutex);
    
    // Create new node
    LRUNode* newNode = (LRUNode*)malloc(sizeof(LRUNode));
    newNode->path = strdup(path);
    newNode->server = server;

    printf("new node");
    
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
    printf("added to cache");
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
        printf("removed from cache");
    } else {
        cache->size++;
        printf("size increased");
    }
    
    pthread_mutex_unlock(&cache->cache_mutex);
}

StorageServer* getFromCache(const char* path) {
    pthread_mutex_lock(&cache->cache_mutex);
    
    unsigned int hash = hashFunction(path, cache->capacity);
    LRUNode* node = cache->hashmap[hash];
    printf("got from cache");
    
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
        printf("moved to front");
        
        // Move to front
        node->next = cache->head;
        node->prev = NULL;
        cache->head->prev = node;
        cache->head = node;
        printf("moved to front --- 2");
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

void *storage_server_listener(void *arg) {
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
    return 0;
}    
