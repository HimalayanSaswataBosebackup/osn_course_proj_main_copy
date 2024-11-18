//common.h
#include <arpa/inet.h>
#define MAX_STORAGE_SERVERS 10
#define MAX_CLIENTS 50
#define BUFFER_SIZE 2048
#define MAX_DIRS 500
#define DIR_SIZE 1024
#include <pthread.h>

typedef struct {
    char ip_address[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    char accessible_paths[MAX_DIRS][DIR_SIZE];
    int num_paths;
    int is_active;
    time_t last_heartbeat;
} StorageServer;
extern pthread_mutex_t num_ss_mutex;

extern int num_storage_servers;
