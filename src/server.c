#include "../include/server.h"
#include "../include/database.h"
#include "../include/commands.h"
#include "../include/utils.h"
#include "../include/persistence.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

// Global variable to store the server socket for signal handler
int server_socket;

// Signal handler for graceful shutdown
void handle_signal(int signal)
{
    printf("\nReceived signal %d. Shutting down server...\n", signal);
    close(server_socket);
    exit(0);
}

// Thread function to handle client connections
void *client_handler(void *arg)
{
    void **thread_args = (void **)arg;
    int client_socket = *((int *)thread_args[0]);
    Database *db = (Database *)thread_args[1];

    // Free the memory allocated for client socket
    free(thread_args[0]);
    free(thread_args);

    // Handle client connection
    handle_client(client_socket, db);

    // Close socket when done
    close(client_socket);
    pthread_exit(NULL);
}

// Function to start the TCP server
bool start_server(Database *db, int port)
{
    struct sockaddr_in server_addr;

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Failed to create socket");
        return false;
    }

    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Failed to set socket options");
        close(server_socket);
        return false;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Failed to bind socket");
        printf("Make sure port %d is not already in use\n", port);
        close(server_socket);
        return false;
    }

    // Listen for connections
    if (listen(server_socket, 10) < 0)
    {
        perror("Failed to listen on socket");
        close(server_socket);
        return false;
    }

    // Set up signal handler for graceful shutdown
    signal(SIGINT, handle_signal);

    printf("Server started on port %d\n", port);

    // Accept and handle client connections
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket;

    while (1)
    {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0)
        {
            perror("Failed to accept connection");
            continue;
        }

        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        // Create thread arguments
        void **thread_args = malloc(2 * sizeof(void *));
        int *client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = client_socket;
        thread_args[0] = client_socket_ptr;
        thread_args[1] = db;

        // Create thread to handle client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, thread_args) != 0)
        {
            perror("Failed to create thread");
            close(client_socket);
            free(client_socket_ptr);
            free(thread_args);
            continue;
        }

        // Detach thread to avoid memory leaks
        pthread_detach(thread_id);
    }

    return true;
}

// Function to handle client connection
void handle_client(int client_socket, Database *db)
{
    char buffer[4096];
    ssize_t bytes_read;
    char command_buffer[8192] = {0}; // Larger buffer to handle fragmented commands
    size_t command_len = 0;

    // Send welcome message
    const char *welcome = "+OK Connected to Key Value Store\r\n";
    if (send(client_socket, welcome, strlen(welcome), 0) < 0)
    {
        perror("Error sending welcome message");
        return;
    }

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        // Receive data
        bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read <= 0)
        {
            if (bytes_read == 0)
            {
                printf("Client disconnected\n");
            }
            else
            {
                perror("Error receiving data");
            }
            break;
        }

        // Null-terminate received data
        buffer[bytes_read] = '\0';

        // Append to command buffer
        if (command_len + bytes_read >= sizeof(command_buffer))
        {
            // Buffer overflow protection
            fprintf(stderr, "Command too large, truncating\n");
            memcpy(command_buffer + command_len, buffer, sizeof(command_buffer) - command_len - 1);
            command_buffer[sizeof(command_buffer) - 1] = '\0';
            command_len = sizeof(command_buffer) - 1;
        }
        else
        {
            memcpy(command_buffer + command_len, buffer, bytes_read);
            command_len += bytes_read;
            command_buffer[command_len] = '\0';
        }

        // Process all complete commands in the buffer
        while (1)
        {
            // Find a complete command
            size_t complete_cmd_len = 0;
            char *complete_cmd = find_complete_resp_command(command_buffer, &complete_cmd_len);

            if (!complete_cmd || complete_cmd_len == 0)
            {
                // No complete command found, wait for more data
                break;
            }

            // Create a temporary buffer to hold just this command
            char *cmd_copy = (char *)malloc(complete_cmd_len + 1);
            if (!cmd_copy)
            {
                fprintf(stderr, "Memory allocation failed\n");
                break;
            }

            // Copy the command and ensure null termination
            memcpy(cmd_copy, complete_cmd, complete_cmd_len);
            cmd_copy[complete_cmd_len] = '\0';

            // Process the complete command
            process_client_command(client_socket, db, cmd_copy);
            free(cmd_copy);

            // Remove the processed command from the buffer
            if (complete_cmd_len < command_len)
            {
                memmove(command_buffer, command_buffer + complete_cmd_len,
                        command_len - complete_cmd_len);
                command_len -= complete_cmd_len;
                command_buffer[command_len] = '\0';
            }
            else
            {
                // All data processed
                command_len = 0;
                command_buffer[0] = '\0';
                break;
            }
        }
    }
}

// Function to send response to client in Redis protocol format
void send_response(int client_socket, const char *response)
{
    size_t total_bytes = strlen(response);
    size_t bytes_sent = 0;

    // Print debug info about what we're sending
    printf("Sending response: '%s' (length: %zu)\n", response, total_bytes);

    // Send data in chunks until everything is sent
    while (bytes_sent < total_bytes)
    {
        ssize_t result = send(client_socket, response + bytes_sent, total_bytes - bytes_sent, 0);

        if (result < 0)
        {
            perror("Error sending response");
            return;
        }

        bytes_sent += result;
    }

    // Ensure all data is sent by flushing (optional)
    // This is typically not needed with TCP sockets but adding for completeness
    // int flag = 1;
    // setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
}

// Function to process commands received from client
void process_client_command(int client_socket, Database *db, const char *command)
{
    printf("Received raw command: '%s'\n", command);

    // Parse the command (might be in RESP format)
    int resp_token_count = 0;
    char *parsed_command = parse_resp((char *)command, &resp_token_count);

    if (!parsed_command)
    {
        printf("Failed to parse command\n");
        send_response(client_socket, "-ERR Invalid command format\r\n");
        return;
    }

    printf("Parsed command: '%s', token count: %d\n", parsed_command, resp_token_count);

    // Remove trailing newline and carriage return
    char clean_command[4096];
    strncpy(clean_command, parsed_command, sizeof(clean_command) - 1);
    clean_command[sizeof(clean_command) - 1] = '\0';
    clean_command[strcspn(clean_command, "\r\n")] = '\0';

    printf("Clean command: '%s'\n", clean_command);

    // Tokenize command
    int token_count = 0;
    char **tokens = tokenise_command(clean_command, &token_count);

    free(parsed_command); // Free the parsed command

    if (token_count == 0)
    {
        printf("No tokens found\n");
        send_response(client_socket, "-ERR Empty command\r\n");
        free_tokens(tokens, token_count);
        return;
    }

    printf("First token: '%s'\n", tokens[0]);

    // Process commands
    char response[4096];
    memset(response, 0, sizeof(response));

    // Handle COMMAND queries (sent by Redis clients to discover commands)
    if (strcasecmp(tokens[0], "COMMAND") == 0)
    {
        // For COMMAND DOCS, return an array of command info
        if (token_count > 1 && strcasecmp(tokens[1], "DOCS") == 0)
        {
            // Just sending a simple array to make redis-cli happy
            snprintf(response, sizeof(response), "*3\r\n$3\r\nSET\r\n$3\r\nGET\r\n$3\r\nDEL\r\n");
            send_response(client_socket, response);
        }
        else
        {
            // Basic response for COMMAND
            send_response(client_socket, "*0\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "SET") == 0)
    {
        if (token_count != 3)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'SET' command\r\n");
        }
        else
        {
            set_command(db, tokens[1], tokens[2]);
            send_response(client_socket, "+OK\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "GET") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'GET' command\r\n");
        }
        else
        {
            char *value = get_command(db, tokens[1]);
            if (value)
            {
                memset(response, 0, sizeof(response));
                snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(value), value);
                send_response(client_socket, response);
            }
            else
            {
                send_response(client_socket, "$-1\r\n"); // Redis nil response
            }
        }
    }
    else if (strcasecmp(tokens[0], "DEL") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'DEL' command\r\n");
        }
        else
        {
            if (del_command(db, tokens[1]))
            {
                send_response(client_socket, ":1\r\n"); // Integer reply format
            }
            else
            {
                send_response(client_socket, ":0\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "EXISTS") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'EXISTS' command\r\n");
        }
        else
        {
            if (exists_command(db, tokens[1]))
            {
                send_response(client_socket, ":1\r\n");
            }
            else
            {
                send_response(client_socket, ":0\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "INCR") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'INCR' command\r\n");
        }
        else
        {
            int new_value;
            if (incr_command(db, tokens[1], &new_value))
            {
                memset(response, 0, sizeof(response));
                snprintf(response, sizeof(response), ":%d\r\n", new_value);
                send_response(client_socket, response);
            }
            else
            {
                send_response(client_socket, "-ERR Value is not an integer or out of range\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "DECR") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'DECR' command\r\n");
        }
        else
        {
            int new_value;
            if (decr_command(db, tokens[1], &new_value))
            {
                memset(response, 0, sizeof(response));
                snprintf(response, sizeof(response), ":%d\r\n", new_value);
                send_response(client_socket, response);
            }
            else
            {
                send_response(client_socket, "-ERR Value is not an integer or out of range\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "SAVE") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'SAVE' command\r\n");
        }
        else
        {
            if (save_command(db, tokens[1]))
            {
                send_response(client_socket, "+OK\r\n");
            }
            else
            {
                send_response(client_socket, "-ERR Failed to save database\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "LOAD") == 0)
    {
        if (token_count != 2)
        {
            send_response(client_socket, "-ERR Wrong number of arguments for 'LOAD' command\r\n");
        }
        else
        {
            if (load_command(db, tokens[1]))
            {
                send_response(client_socket, "+OK\r\n");
            }
            else
            {
                send_response(client_socket, "-ERR Failed to load database\r\n");
            }
        }
    }
    // Redis protocol PING command
    else if (strcasecmp(tokens[0], "PING") == 0)
    {
        const char *pong = (token_count == 2) ? tokens[1] : "PONG";
        memset(response, 0, sizeof(response));
        snprintf(response, sizeof(response), "+%s\r\n", pong);
        send_response(client_socket, response);
    }
    // Simple command to get server info
    else if (strcasecmp(tokens[0], "INFO") == 0)
    {
        const char *info_text = "# Server\r\nkey_value_store_version:1.0\r\nprotocol_version:1.0\r\n";
        memset(response, 0, sizeof(response));
        snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(info_text), info_text);
        send_response(client_socket, response);
    }
    // Command to exit
    else if (strcasecmp(tokens[0], "QUIT") == 0 || strcasecmp(tokens[0], "EXIT") == 0)
    {
        send_response(client_socket, "+OK\r\n");
        free_tokens(tokens, token_count);
        return;
    }
    else
    {
        memset(response, 0, sizeof(response));
        snprintf(response, sizeof(response), "-ERR Unknown command '%s'\r\n", tokens[0]);
        send_response(client_socket, response);
    }

    free_tokens(tokens, token_count);
}