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
#include <errno.h>

// Global variables for server management
static int g_server_socket = -1;
static volatile bool g_server_running = true;
static int g_active_connections = 0;
static pthread_mutex_t g_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

// Maximum connections and buffer sizes
#define MAX_CONNECTIONS 100
#define INITIAL_BUFFER_SIZE 4096
#define MAX_BUFFER_SIZE (1024 * 1024) // 1MB max command size
#define MAX_COMMAND_SIZE (512 * 1024) // 512KB max single command

// Dynamic buffer structure
typedef struct
{
    char *data;
    size_t size;
    size_t capacity;
} DynamicBuffer;

// Initialize dynamic buffer
static bool buffer_init(DynamicBuffer *buf, size_t initial_capacity)
{
    buf->data = malloc(initial_capacity);
    if (!buf->data)
        return false;

    buf->size = 0;
    buf->capacity = initial_capacity;
    buf->data[0] = '\0';
    return true;
}

// Append data to dynamic buffer
static bool buffer_append(DynamicBuffer *buf, const char *data, size_t len)
{
    if (buf->size + len + 1 > buf->capacity)
    {
        // Need to resize
        size_t new_capacity = buf->capacity * 2;
        while (new_capacity < buf->size + len + 1)
            new_capacity *= 2;

        if (new_capacity > MAX_BUFFER_SIZE)
        {
            fprintf(stderr, "Buffer size limit exceeded\n");
            return false;
        }

        char *new_data = realloc(buf->data, new_capacity);
        if (!new_data)
        {
            fprintf(stderr, "Failed to reallocate buffer\n");
            return false;
        }

        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
    return true;
}

// Remove data from beginning of buffer
static void buffer_consume(DynamicBuffer *buf, size_t len)
{
    if (len >= buf->size)
    {
        buf->size = 0;
        buf->data[0] = '\0';
        return;
    }

    memmove(buf->data, buf->data + len, buf->size - len);
    buf->size -= len;
    buf->data[buf->size] = '\0';
}

// Free dynamic buffer
static void buffer_free(DynamicBuffer *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

// Thread argument structure
typedef struct
{
    int client_socket;
    Database *db;
    struct sockaddr_in client_addr;
} ThreadArgs;

// Signal handler for graceful shutdown
void handle_signal(int signal)
{
    printf("\nReceived signal %d. Shutting down server...\n", signal);
    g_server_running = false;

    if (g_server_socket >= 0)
    {
        close(g_server_socket);
        g_server_socket = -1;
    }

    // Give some time for connections to close
    sleep(1);
    exit(0);
}

// Thread function to handle client connections
void *client_handler(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    int client_socket = args->client_socket;
    Database *db = args->db;
    struct sockaddr_in client_addr = args->client_addr;

    printf("Thread started for client %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    // Increment connection counter
    pthread_mutex_lock(&g_connection_mutex);
    g_active_connections++;
    pthread_mutex_unlock(&g_connection_mutex);

    // Handle client connection
    handle_client(client_socket, db);

    // Cleanup
    close(client_socket);
    free(args);

    // Decrement connection counter
    pthread_mutex_lock(&g_connection_mutex);
    g_active_connections--;
    pthread_mutex_unlock(&g_connection_mutex);

    printf("Client %s:%d disconnected\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    pthread_exit(NULL);
}

// Function to start the TCP server
bool start_server(Database *db, int port)
{
    struct sockaddr_in server_addr;

    // Create socket
    g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_socket < 0)
    {
        perror("Failed to create socket");
        return false;
    }

    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Failed to set socket options");
        close(g_server_socket);
        return false;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket
    if (bind(g_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Failed to bind socket");
        printf("Make sure port %d is not already in use\n", port);
        close(g_server_socket);
        return false;
    }

    // Listen for connections
    if (listen(g_server_socket, 10) < 0)
    {
        perror("Failed to listen on socket");
        close(g_server_socket);
        return false;
    }

    // Set up signal handler for graceful shutdown
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Server started on port %d (max connections: %d)\n", port, MAX_CONNECTIONS);

    // Accept and handle client connections
    while (g_server_running)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(g_server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket < 0)
        {
            if (errno == EINTR && !g_server_running)
                break; // Server shutdown

            perror("Failed to accept connection");
            continue;
        }

        // Check connection limit
        pthread_mutex_lock(&g_connection_mutex);
        int current_connections = g_active_connections;
        pthread_mutex_unlock(&g_connection_mutex);

        if (current_connections >= MAX_CONNECTIONS)
        {
            printf("Connection limit reached, rejecting client %s:%d\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));

            const char *error_msg = "-ERR Server busy, too many connections\r\n";
            send(client_socket, error_msg, strlen(error_msg), 0);
            close(client_socket);
            continue;
        }

        printf("New connection from %s:%d (active: %d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               current_connections + 1);

        // Create thread arguments
        ThreadArgs *thread_args = malloc(sizeof(ThreadArgs));
        if (!thread_args)
        {
            perror("Failed to allocate thread arguments");
            close(client_socket);
            continue;
        }

        thread_args->client_socket = client_socket;
        thread_args->db = db;
        thread_args->client_addr = client_addr;

        // Create thread to handle client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, thread_args) != 0)
        {
            perror("Failed to create thread");
            close(client_socket);
            free(thread_args);
            continue;
        }

        // Detach thread to avoid memory leaks
        pthread_detach(thread_id);
    }

    printf("Server shutting down...\n");
    return true;
}

// Function to handle client connection with proper buffer management
void handle_client(int client_socket, Database *db)
{
    DynamicBuffer command_buffer;
    char recv_buffer[4096];
    ssize_t bytes_read;

    printf("DEBUG: Starting handle_client\n");

    // Initialize dynamic buffer
    if (!buffer_init(&command_buffer, INITIAL_BUFFER_SIZE))
    {
        fprintf(stderr, "Failed to initialize command buffer\n");
        return;
    }

    // Send welcome message
    // const char *welcome = "+OK Connected to Key Value Store\r\n";
    // send_response_debug(client_socket, welcome);

    // printf("DEBUG: Sent welcome message\n");

    while (g_server_running)
    {
        // Receive data
        bytes_read = recv(client_socket, recv_buffer, sizeof(recv_buffer), 0);

        if (bytes_read <= 0)
        {
            if (bytes_read == 0)
            {
                printf("Client disconnected gracefully\n");
            }
            else if (errno != EINTR)
            {
                perror("Error receiving data");
            }
            break;
        }

        printf("DEBUG: Received %zd bytes\n", bytes_read);

        // Append to command buffer
        if (!buffer_append(&command_buffer, recv_buffer, bytes_read))
        {
            send_response_debug(client_socket, "-ERR Command too large\r\n");
            break;
        }

        printf("DEBUG: Buffer now contains %zu bytes\n", command_buffer.size);

        // Process all complete commands in the buffer
        while (command_buffer.size > 0)
        {
            size_t command_len = 0;
            char *complete_cmd = find_complete_resp_command(
                command_buffer.data, command_buffer.size, &command_len);

            if (!complete_cmd || command_len == 0)
            {
                // No complete command found, wait for more data
                printf("DEBUG: No complete command found, waiting for more data\n");
                break;
            }

            printf("DEBUG: Found complete command of length %zu\n", command_len);

            // Check command size limit
            if (command_len > MAX_COMMAND_SIZE)
            {
                send_response_debug(client_socket, "-ERR Command too large\r\n");
                buffer_consume(&command_buffer, command_len);
                continue;
            }

            // Create a copy of the command for processing
            char *cmd_copy = malloc(command_len + 1);
            if (!cmd_copy)
            {
                fprintf(stderr, "Memory allocation failed\n");
                send_response_debug(client_socket, "-ERR Out of memory\r\n");
                break;
            }

            memcpy(cmd_copy, complete_cmd, command_len);
            cmd_copy[command_len] = '\0';

            printf("DEBUG: Processing command: '%.50s%s'\n", cmd_copy,
                   (command_len > 50) ? "..." : "");

            // Process the complete command
            process_client_command(client_socket, db, cmd_copy);
            free(cmd_copy);

            // Remove the processed command from the buffer
            buffer_consume(&command_buffer, command_len);
        }
    }

    buffer_free(&command_buffer);
    printf("DEBUG: Finished handle_client\n");
}

// Function to process commands received from client
void process_client_command(int client_socket, Database *db, const char *command)
{
    printf("DEBUG: process_client_command called with: '%.100s%s'\n",
           command, (strlen(command) > 100) ? "..." : "");

    if (!command || !*command)
    {
        send_response_debug(client_socket, "-ERR Empty command\r\n");
        return;
    }

    // Parse the command (might be in RESP format)
    int resp_token_count = 0;
    size_t command_len = strlen(command);
    char *parsed_command = parse_resp(command, command_len, &resp_token_count);

    printf("DEBUG: parse_resp returned: %s\n", parsed_command ? "valid" : "NULL");

    if (!parsed_command)
    {
        printf("Failed to parse command: '%.*s'\n", (int)command_len, command);
        send_response_debug(client_socket, "-ERR Invalid command format\r\n");
        return;
    }

    // Remove trailing CRLF
    char *clean_command = malloc(strlen(parsed_command) + 1);
    if (!clean_command)
    {
        free(parsed_command);
        send_response_debug(client_socket, "-ERR Out of memory\r\n");
        return;
    }

    strcpy(clean_command, parsed_command);
    clean_command[strcspn(clean_command, "\r\n")] = '\0';
    free(parsed_command);

    printf("DEBUG: Clean command: '%s'\n", clean_command);

    // Tokenize command
    int token_count = 0;
    char **tokens = tokenise_command(clean_command, &token_count);
    free(clean_command);

    printf("DEBUG: Tokenized into %d tokens\n", token_count);

    if (token_count == 0 || !tokens)
    {
        send_response_debug(client_socket, "-ERR Empty command\r\n");
        if (tokens)
            free_tokens(tokens, token_count);
        return;
    }

    printf("DEBUG: First token: '%s'\n", tokens[0]);

    // Process commands
    char response[4096];

    // Handle COMMAND queries (sent by Redis clients to discover commands)
    if (strcasecmp(tokens[0], "COMMAND") == 0)
    {
        printf("DEBUG: Processing COMMAND\n");
        if (token_count > 1 && strcasecmp(tokens[1], "DOCS") == 0)
        {
            // Return empty array for COMMAND DOCS to keep it simple
            send_response_debug(client_socket, "*0\r\n");
        }
        else
        {
            // Return array of supported commands
            const char *command_list =
                "*10\r\n"
                "$3\r\nSET\r\n"
                "$3\r\nGET\r\n"
                "$3\r\nDEL\r\n"
                "$6\r\nEXISTS\r\n"
                "$4\r\nINCR\r\n"
                "$4\r\nDECR\r\n"
                "$4\r\nPING\r\n"
                "$6\r\nEXPIRE\r\n"
                "$3\r\nTTL\r\n"
                "$7\r\nPERSIST\r\n";
            send_response_debug(client_socket, command_list);
        }
    }
    else if (strcasecmp(tokens[0], "SET") == 0)
    {
        printf("DEBUG: Processing SET\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'set' command\r\n");
        }
        else
        {
            set_command(db, tokens[1], tokens[2]);
            send_response_debug(client_socket, "+OK\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "GET") == 0)
    {
        printf("DEBUG: Processing GET\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'get' command\r\n");
        }
        else
        {
            char *value = get_command(db, tokens[1]);
            if (value)
            {
                // memset(response, 0, sizeof(response)); To initialize or clear the buffer so it doesn't contain any leftover data.
                snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(value), value);
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "$-1\r\n"); // Redis nil response
            }
        }
    }
    else if (strcasecmp(tokens[0], "DEL") == 0)
    {
        printf("DEBUG: Processing DEL\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'del' command\r\n");
        }
        else
        {
            bool deleted = del_command(db, tokens[1]);
            send_response_debug(client_socket, deleted ? ":1\r\n" : ":0\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "EXISTS") == 0)
    {
        printf("DEBUG: Processing EXISTS\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'exists' command\r\n");
        }
        else
        {
            bool exists = exists_command(db, tokens[1]);
            send_response_debug(client_socket, exists ? ":1\r\n" : ":0\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "INCR") == 0)
    {
        printf("DEBUG: Processing INCR\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'incr' command\r\n");
        }
        else
        {
            int new_value;
            if (incr_command(db, tokens[1], &new_value))
            {
                snprintf(response, sizeof(response), ":%d\r\n", new_value);
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "-ERR value is not an integer or out of range\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "DECR") == 0)
    {
        printf("DEBUG: Processing DECR\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'decr' command\r\n");
        }
        else
        {
            int new_value;
            if (decr_command(db, tokens[1], &new_value))
            {
                snprintf(response, sizeof(response), ":%d\r\n", new_value);
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "-ERR value is not an integer or out of range\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "EXPIRE") == 0)
    {
        printf("DEBUG: Processing EXPIRE\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'expire' command\r\n");
        }
        else
        {
            int seconds = atoi(tokens[2]);
            if (seconds < 0)
            {
                send_response_debug(client_socket, "-ERR invalid expire time\r\n");
            }
            else
            {
                bool result = expire_command(db, tokens[1], seconds);
                send_response_debug(client_socket, result ? ":1\r\n" : ":0\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "TTL") == 0)
    {
        printf("DEBUG: Processing TTL\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'ttl' command\r\n");
        }
        else
        {
            int ttl = ttl_command(db, tokens[1]);
            snprintf(response, sizeof(response), ":%d\r\n", ttl);
            send_response_debug(client_socket, response);
        }
    }
    else if (strcasecmp(tokens[0], "PERSIST") == 0)
    {
        printf("DEBUG: Processing PERSIST\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'persist' command\r\n");
        }
        else
        {
            bool result = persist_command(db, tokens[1]);
            send_response_debug(client_socket, result ? ":1\r\n" : ":0\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "SAVE") == 0)
    {
        printf("DEBUG: Processing SAVE\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'save' command\r\n");
        }
        else
        {
            if (save_command(db, tokens[1]))
            {
                send_response_debug(client_socket, "+OK\r\n");
            }
            else
            {
                send_response_debug(client_socket, "-ERR Failed to save database\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "LOAD") == 0)
    {
        printf("DEBUG: Processing LOAD\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'load' command\r\n");
        }
        else
        {
            if (load_command(db, tokens[1]))
            {
                send_response_debug(client_socket, "+OK\r\n");
            }
            else
            {
                send_response_debug(client_socket, "-ERR Failed to load database\r\n");
            }
        }
    }
    // Redis protocol PING command
    else if (strcasecmp(tokens[0], "PING") == 0)
    {
        printf("DEBUG: Processing PING\n");
        if (token_count == 1)
        {
            send_response_debug(client_socket, "+PONG\r\n");
        }
        else if (token_count == 2)
        {
            snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(tokens[1]), tokens[1]);
            send_response_debug(client_socket, response);
        }
        else
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'ping' command\r\n");
        }
    }
    // Simple command to get server info
    else if (strcasecmp(tokens[0], "INFO") == 0)
    {
        printf("DEBUG: Processing INFO\n");
        const char *info_text = "# Server\r\nkey_value_store_version:1.0\r\nprotocol_version:1.0";
        snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(info_text), info_text);
        send_response_debug(client_socket, response);
    }
    // Command to exit
    else if (strcasecmp(tokens[0], "QUIT") == 0 || strcasecmp(tokens[0], "EXIT") == 0)
    {
        printf("DEBUG: Processing QUIT/EXIT\n");
        send_response_debug(client_socket, "+OK\r\n");
        free_tokens(tokens, token_count);
        return;
    }
    else
    {
        printf("DEBUG: Unknown command: '%s'\n", tokens[0]);
        snprintf(response, sizeof(response), "-ERR unknown command '%s'\r\n", tokens[0]);
        send_response_debug(client_socket, response);
    }

    free_tokens(tokens, token_count);
    printf("DEBUG: Finished processing command\n");
}

// Debug function to help diagnose RESP formatting issues
void debug_resp_response(const char *label, const char *resp_data)
{
    printf("DEBUG RESP [%s]: ", label);
    for (int i = 0; resp_data[i] != '\0'; i++)
    {
        char c = resp_data[i];
        if (c == '\r')
            printf("\\r");
        else if (c == '\n')
            printf("\\n");
        else if (c >= 32 && c <= 126)
            printf("%c", c);
        else
            printf("\\x%02x", (unsigned char)c);
    }
    printf("\n");
}

// Enhanced send_response_debug function with debugging
void send_response_debug(int client_socket, const char *response)
{
    if (!response)
        return;

    // Debug the response before sending
    debug_resp_response("SENDING", response);

    size_t total_bytes = strlen(response);
    size_t bytes_sent = 0;

    printf("DEBUG: Sending response: '%.100s%s'\n", response,
           (total_bytes > 100) ? "..." : "");

    // Send data in chunks until everything is sent
    while (bytes_sent < total_bytes)
    {
        ssize_t result = send(client_socket, response + bytes_sent,
                              total_bytes - bytes_sent, MSG_NOSIGNAL);

        if (result < 0)
        {
            if (errno == EPIPE || errno == ECONNRESET)
            {
                // Client disconnected
                break;
            }
            perror("Error sending response");
            return;
        }

        bytes_sent += result;
    }
}