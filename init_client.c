// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

#define INITIAL_ACK "INITIAL_ACK"
#define FINAL_ACK "FINAL_ACK"
#define ACK_TIMEOUT 5

void display_menu() {
    printf("\nAvailable Commands:\n");
    printf("1. READ <path>\n");
    printf("2. STREAM <path>\n");
    printf("3. CREATE <path> <name>\n");
    printf("4. COPY <source> <dest>\n");
    printf("5. DELETE <path>\n");
    printf("6. EXIT\n\n");
    printf("Enter your command: ");
}

void handle_command(int socket_fd, char *command) {
    char buffer[BUFFER_SIZE] = {0};
    
    // Send command to the server
    if (send(socket_fd, command, strlen(command), 0) < 0) {
        perror("Failed to send command");
        return;
    }
    else{
        printf("Command Sent: %s\n", command);
    }

    // Wait for initial acknowledgment
    struct timeval tv;
    tv.tv_sec = ACK_TIMEOUT;
    tv.tv_usec = 0;
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_fd, &readfds);
    
    int ready = select(socket_fd + 1, &readfds, NULL, NULL, &tv);
    if (ready <= 0) {
        printf("Timeout waiting for initial acknowledgment\n");
        return;
    }

    // Receive initial ACK
    int received = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
    if (received < 0 || strncmp(buffer, INITIAL_ACK, strlen(INITIAL_ACK)) != 0) {
        printf("Failed to receive initial acknowledgment\n");
        return;
    }
    else{
        printf("Request acknowledged, processing...\n");
    }




    // Wait for final response
    memset(buffer, 0, BUFFER_SIZE);
    received = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
    if (received < 0) {
        perror("Failed to receive final response");
        return;
    }
    
    if (strncmp(buffer, FINAL_ACK, strlen(FINAL_ACK)) == 0) {
        printf("Operation completed successfully\n");
    } else {
        buffer[received] = '\0';
        printf("Server Response:\n%s\n", buffer);
    }
}

void handle_timeout(int signum) {
    printf("Operation timed out\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <naming_server_ip> <naming_server_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    // Create socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // Configure server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    // Connect to the server
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Naming Server failed");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    printf("Connected to Naming Server at %s:%d\n", server_ip, server_port);

    char command[BUFFER_SIZE];
    while (1) {
        display_menu();
        if (!fgets(command, sizeof(command), stdin)) {
            printf("Failed to read input\n");
            break;
        }

        // Remove newline character
        command[strcspn(command, "\n")] = 0;

        // Exit if the command is "EXIT"
        if (strcasecmp(command, "EXIT") == 0) {
            printf("Exiting...\n");
            break;
        }

        handle_command(socket_fd, command);
    }

    close(socket_fd);
    return EXIT_SUCCESS;
}