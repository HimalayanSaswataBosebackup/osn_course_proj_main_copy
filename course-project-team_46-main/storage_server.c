// storage_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define NAMING_SERVER_IP "127.0.0.1"
#define NAMING_SERVER_PORT 8080
#define STORAGE_SERVER_PORT 8081
#define BUFFER_SIZE 1024
#define MAX_FILES 100

typedef struct {
    char filename[50];
    char filepath[100];
} FileInfo;


FileInfo files[MAX_FILES];
int file_count = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define NAMING_SERVER_IP "127.0.0.1"
#define NAMING_SERVER_PORT 8080
#define STORAGE_SERVER_PORT 8081
#define BUFFER_SIZE 1024
#define MAX_FILES 100
#define DIRECTORY_PATH "./files"

// Structure to hold file and directory information
typedef struct {
    char name[50];
    char path[100];
    int is_directory;  // 1 if directory, 0 if file
} FileInfo;

FileInfo files[MAX_FILES];
int file_count = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;



// int main() {
//     pthread_t thread_id;

//     // Create the files directory if it doesn't exist
//     mkdir(DIRECTORY_PATH, 0777);

//     // Start the thread to handle commands from the Naming Server
//     if (pthread_create(&thread_id, NULL, handle_naming_server_commands, NULL) != 0) {
//         perror("Failed to create thread");
//         exit(1);
//     }

//     pthread_join(thread_id, NULL);
//     return 0;
// }

// // Thread to handle commands from the Naming Server
// void *handle_naming_server_commands(void *arg) {
//     int sockfd;
//     struct sockaddr_in server_addr;
//     char buffer[BUFFER_SIZE];

//     // Set up the socket to listen for commands from the Naming Server
//     if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//         perror("Socket creation failed");
//         exit(1);
//     }

//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(STORAGE_SERVER_PORT);
//     server_addr.sin_addr.s_addr = inet_addr(NAMING_SERVER_IP);

//     // Connect to the Naming Server
//     if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Connection to Naming Server failed");
//         exit(1);
//     }

//     while (1) {
//         // Receive command from Naming Server
//         memset(buffer, 0, BUFFER_SIZE);
//         if (recv(sockfd, buffer, BUFFER_SIZE, 0) <= 0) {
//             perror("Error receiving command");
//             break;
//         }

//         // Parse the command
//         char command[10], name[50], destination[50];
//         sscanf(buffer, "%s %s %s", command, name, destination);

//         pthread_mutex_lock(&file_mutex);

//         if (strcmp(command, "CREATE_FILE") == 0) {
//             create_empty_file(name);
//         } else if (strcmp(command, "DELETE_FILE") == 0) {
//             delete_file(name);
//         } else if (strcmp(command, "COPY_FILE") == 0) {
//             copy_file(name, destination);
//         } else if (strcmp(command, "CREATE_DIR") == 0) {
//             create_directory(name);
//         } else if (strcmp(command, "DELETE_DIR") == 0) {
//             delete_directory(name);
//         } else if (strcmp(command, "COPY_DIR") == 0) {
//             copy_directory(name, destination);
//         } else {
//             printf("Unknown command received: %s\n", command);
//         }

//         pthread_mutex_unlock(&file_mutex);
//     }

//     close(sockfd);
//     return NULL;
// }

// // Function to create an empty file in the storage server directory
// void create_empty_file(const char *filename) {
//     char filepath[100];
//     snprintf(filepath, sizeof(filepath), "%s/%s", DIRECTORY_PATH, filename);

//     FILE *file = fopen(filepath, "w");
//     if (file) {
//         fclose(file);
//         printf("Created empty file: %s\n", filepath);

//         // Append to the file list
//         snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", filename);
//         snprintf(files[file_count].path, sizeof(files[file_count].path), "%s", filepath);
//         files[file_count].is_directory = 0;
//         file_count++;
//     } else {
//         perror("Error creating file");
//     }
// }

// // Function to delete a file from the storage server directory
// void delete_file(const char *filename) {
//     char filepath[100];
//     snprintf(filepath, sizeof(filepath), "%s/%s", DIRECTORY_PATH, filename);

//     if (remove(filepath) == 0) {
//         printf("Deleted file: %s\n", filepath);

//         // Remove file from file list
//         for (int i = 0; i < file_count; i++) {
//             if (strcmp(files[i].name, filename) == 0 && files[i].is_directory == 0) {
//                 for (int j = i; j < file_count - 1; j++) {
//                     files[j] = files[j + 1];
//                 }
//                 file_count--;
//                 break;
//             }
//         }
//     } else {
//         perror("Error deleting file");
//     }
// }

// // Function to copy a file within the storage server directory
// void copy_file(const char *source_filename, const char *destination_filename) {
//     char source_path[100], destination_path[100];
//     snprintf(source_path, sizeof(source_path), "%s/%s", DIRECTORY_PATH, source_filename);
//     snprintf(destination_path, sizeof(destination_path), "%s/%s", DIRECTORY_PATH, destination_filename);

//     FILE *source = fopen(source_path, "r");
//     FILE *dest = fopen(destination_path, "w");

//     if (source && dest) {
//         char ch;
//         while ((ch = fgetc(source)) != EOF) {
//             fputc(ch, dest);
//         }
//         fclose(source);
//         fclose(dest);
//         printf("Copied file from %s to %s\n", source_path, destination_path);

//         // Add the copied file to file list
//         snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", destination_filename);
//         snprintf(files[file_count].path, sizeof(files[file_count].path), "%s", destination_path);
//         files[file_count].is_directory = 0;
//         file_count++;
//     } else {
//         perror("Error copying file");
//     }
// }

// // Function to create a new directory
// void create_directory(const char *dirname) {
//     char dirpath[100];
//     snprintf(dirpath, sizeof(dirpath), "%s/%s", DIRECTORY_PATH, dirname);

//     if (mkdir(dirpath, 0777) == 0) {
//         printf("Created directory: %s\n", dirpath);

//         // Add directory to file list
//         snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", dirname);
//         snprintf(files[file_count].path, sizeof(files[file_count].path), "%s", dirpath);
//         files[file_count].is_directory = 1;
//         file_count++;
//     } else {
//         perror("Error creating directory");
//     }
// }

// // Function to delete a directory and its contents recursively
// void delete_directory(const char *dirname) {
//     char dirpath[100];
//     snprintf(dirpath, sizeof(dirpath), "%s/%s", DIRECTORY_PATH, dirname);

//     DIR *d = opendir(dirpath);
//     struct dirent *entry;
//     if (d) {
//         while ((entry = readdir(d)) != NULL) {
//             if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
//                 char entrypath[150];
//                 snprintf(entrypath, sizeof(entrypath), "%s/%s", dirpath, entry->d_name);
//                 if (entry->d_type == DT_DIR) {
//                     delete_directory(entrypath);  // Recursively delete subdirectories
//                 } else {
//                     remove(entrypath);
//                 }
//             }
//         }
//         closedir(d);
//         rmdir(dirpath);
//         printf("Deleted directory: %s\n", dirpath);
//     } else {
//         perror("Error deleting directory");
//     }
// }

// // Function to copy a directory and its contents recursively
// void copy_directory(const char *source_dirname, const char *destination_dirname) {
//     char source_path[100], destination_path[100];
//     snprintf(source_path, sizeof(source_path), "%s/%s", DIRECTORY_PATH, source_dirname);
//     snprintf(destination_path, sizeof(destination_path), "%s/%s", DIRECTORY_PATH, destination_dirname);

//     mkdir(destination_path, 0777);  // Create destination directory
//     copy_directory_contents(source_path, destination_path);
//     printf("Copied directory from %s to %s\n", source_path, destination_path);

//     // Add the copied directory to file list
//     snprintf(files[file_count].name, sizeof(files[file_count].name), "%s", destination_dirname);
//     snprintf(files[file_count].path, sizeof(files[file_count].path), "%s", destination_path);
//     files[file_count].is_directory = 1;
//     file_count++;
// }

// // Helper function to copy contents of a directory
// void copy_directory_contents(const char *source, const char *destination) {
//     DIR *d = opendir(source);
//     struct dirent *entry;

//     if (d) {
//         while ((entry = readdir(d)) != NULL) {
//             if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
//                 char source_entry[150], dest_entry[150];
//                 snprintf(source_entry, sizeof(source_entry), "%s/%s", source, entry->d_name);
//                 snprintf(dest_entry, sizeof(dest_entry), "%s/%s", destination, entry->d_name);

//                 if (entry->d_type == DT_DIR) {
//                     mkdir(dest_entry, 0777);
//                     copy_directory_contents(source_entry, dest_entry);
//                 } else {
//                     FILE *src = fopen(source_entry, "r");
//                     FILE *dst = fopen(dest_entry, "w");

//                     if (src && dst) {
//                         char ch;
//                         while ((ch = fgetc(src)) != EOF) {
//                             fputc(ch, dst);
//                         }
//                         fclose(src);
//                         fclose(dst);
//                     }
//                 }
//             }
//         }
//         closedir(d);
//     }
// }







// void handle_read_file(int client_socket, const char *filename) {
//     pthread_mutex_lock(&file_mutex);

//     char buffer[BUFFER_SIZE];
//     int found = 0;
//     for (int i = 0; i < file_count; i++) {
//         if (strcmp(files[i].filename, filename) == 0) {
//             FILE *file = fopen(files[i].filepath, "r");
//             if (file == NULL) {
//                 perror("Failed to open file");
//                 send(client_socket, "Error reading file\n", strlen("Error reading file\n"), 0);
//             } else {
//                 fread(buffer, sizeof(char), BUFFER_SIZE, file);
//                 send(client_socket, buffer, strlen(buffer), 0);
//                 fclose(file);
//             }
//             found = 1;
//             break;
//         }
//     }

//     if (!found) {
//         send(client_socket, "File Not Found\n", strlen("File Not Found\n"), 0);
//     }

//     pthread_mutex_unlock(&file_mutex);
// }




