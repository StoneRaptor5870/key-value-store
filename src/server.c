#include "../include/server.h"
#include "../include/database.h"
#include "../include/commands.h"
#include "../include/utils.h"
#include "../include/persistence.h"
#include "../include/pubsub.h"
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
static PubSubManager *g_pubsub_manager = NULL;

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
    PubSubManager *pubsub;
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

    if (g_pubsub_manager)
    {
        pubsub_free(g_pubsub_manager);
        g_pubsub_manager = NULL;
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
    // PubSubManager *pubsub = args->pubsub;
    struct sockaddr_in client_addr = args->client_addr;

    printf("Thread started for client %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    // Increment connection counter
    pthread_mutex_lock(&g_connection_mutex);
    g_active_connections++;
    pthread_mutex_unlock(&g_connection_mutex);

    // Handle client connection
    handle_client(client_socket, db, g_pubsub_manager);

    // Unsubscribe from all channels when client disconnects
    if (g_pubsub_manager)
    {
        pubsub_unsubscribe_all(g_pubsub_manager, client_socket);
    }

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

    // Create the global pubsub manager
    g_pubsub_manager = pubsub_create();
    if (!g_pubsub_manager)
    {
        fprintf(stderr, "Failed to create pub/sub manager\n");
        close(g_server_socket);
        return false;
    }

    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Failed to set socket options");
        close(g_server_socket);
        pubsub_free(g_pubsub_manager);
        g_pubsub_manager = NULL;
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
        pubsub_free(g_pubsub_manager);
        g_pubsub_manager = NULL;
        return false;
    }

    // Listen for connections
    if (listen(g_server_socket, 10) < 0)
    {
        perror("Failed to listen on socket");
        close(g_server_socket);
        pubsub_free(g_pubsub_manager);
        g_pubsub_manager = NULL;
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
        thread_args->pubsub = g_pubsub_manager;
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

    // Clean up pubsub manager
    if (g_pubsub_manager)
    {
        pubsub_free(g_pubsub_manager);
        g_pubsub_manager = NULL;
    }

    return true;
}

void send_http_response(int client_socket, const char *status, const char *body) {
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s", status, strlen(body), body);
    
    send(client_socket, response, strlen(response), 0);
}

void handle_http_request(int client_socket, const char *request) {
    if (strstr(request, "GET /health") || strstr(request, "GET /")) {
        send_http_response(client_socket, "200 OK", "OK");
    } else {
        send_http_response(client_socket, "404 Not Found", "Not Found");
    }
}

// Function to handle client connection with proper buffer management
void handle_client(int client_socket, Database *db, PubSubManager *pubsub)
{
    DynamicBuffer command_buffer;
    char recv_buffer[4096];
    ssize_t bytes_read;
    int is_http_request = 0;

    printf("DEBUG: Starting handle_client\n");

    // Initialize dynamic buffer
    if (!buffer_init(&command_buffer, INITIAL_BUFFER_SIZE))
    {
        fprintf(stderr, "Failed to initialize command buffer\n");
        return;
    }

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

        // Check if this is the first data received and if it's an HTTP request
        if (command_buffer.size == 0 && bytes_read >= 4 && 
            strncmp(recv_buffer, "GET ", 4) == 0) {
            is_http_request = 1;
        }

        // Append to command buffer
        if (!buffer_append(&command_buffer, recv_buffer, bytes_read))
        {
            if (!is_http_request) {
                send_response_debug(client_socket, "-ERR Command too large\r\n");
            }
            break;
        }

        printf("DEBUG: Buffer now contains %zu bytes\n", command_buffer.size);

        // Handle HTTP request
        if (is_http_request) {
            // Null-terminate the buffer for string operations
            char *null_term_buffer = malloc(command_buffer.size + 1);
            if (!null_term_buffer) {
                fprintf(stderr, "Memory allocation failed\n");
                break;
            }
            memcpy(null_term_buffer, command_buffer.data, command_buffer.size);
            null_term_buffer[command_buffer.size] = '\0';
            
            // Check if we have a complete HTTP request (ends with \r\n\r\n)
            if (strstr(null_term_buffer, "\r\n\r\n")) {
                handle_http_request(client_socket, null_term_buffer);
                free(null_term_buffer);
                break; // Close connection after HTTP response
            }
            free(null_term_buffer);
            continue; // Wait for more HTTP data
        }

        // Process Redis protocol commands
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
            process_client_command(client_socket, db, pubsub, cmd_copy);
            free(cmd_copy);

            // Remove the processed command from the buffer
            buffer_consume(&command_buffer, command_len);
        }
    }

    buffer_free(&command_buffer);
    printf("DEBUG: Finished handle_client\n");
}

// Function to process commands received from client
void process_client_command(int client_socket, Database *db, PubSubManager *pubsub, const char *command)
{
    printf("DEBUG: process_client_command called with: '%.100s%s'\n",
           command, (strlen(command) > 100) ? "..." : "");

    if (!command || !*command)
    {
        send_response_debug(client_socket, "-ERR Empty command\r\n");
        return;
    }

    // Parse the command (RESP format)
    int token_count = 0;
    size_t command_len = strlen(command);
    char **tokens = parse_resp_tokens(command, command_len, &token_count);

    printf("DEBUG: parse_resp_tokens returned: %s with %d tokens\n", tokens ? "valid" : "NULL", token_count);

    if (!tokens || token_count == 0)
    {
        printf("Failed to parse command: '%.*s'\n", (int)command_len, command);
        send_response_debug(client_socket, "-ERR Invalid command format\r\n");
        return;
    }

    printf("DEBUG: First token: '%s'\n", tokens[0]);
    for (int i = 0; i < token_count; i++)
    {
        printf("DEBUG: Token %d: '%s'\n", i, tokens[i]);
    }

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
                "*24\r\n"
                "$3\r\nSET\r\n"
                "$3\r\nGET\r\n"
                "$3\r\nDEL\r\n"
                "$6\r\nEXISTS\r\n"
                "$4\r\nINCR\r\n"
                "$4\r\nDECR\r\n"
                "$4\r\nPING\r\n"
                "$6\r\nEXPIRE\r\n"
                "$3\r\nTTL\r\n"
                "$7\r\nPERSIST\r\n"
                "$5\r\nLPUSH\r\n"
                "$5\r\nRPUSH\r\n"
                "$4\r\nLPOP\r\n"
                "$4\r\nRPOP\r\n"
                "$6\r\nLRANGE\r\n"
                "$4\r\nLLEN\r\n"
                "$4\r\nHSET\r\n"
                "$4\r\nHGET\r\n"
                "$7\r\nHGETALL\r\n"
                "$4\r\nHDEL\r\n"
                "$7\r\nHEXISTS\r\n"
                "$9\r\nSUBSCRIBE\r\n"
                "$11\r\nUNSUBSCRIBE\r\n"
                "$7\r\nPUBLISH\r\n";
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
    else if (strcasecmp(tokens[0], "LPUSH") == 0)
    {
        printf("DEBUG: Processing LPUSH\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'lpush' command\r\n");
        }
        else
        {
            if (lpush_command(db, tokens[1], tokens[2]))
            {
                int new_length = llen_command(db, tokens[1]);
                snprintf(response, sizeof(response), ":%d\r\n", new_length);
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "-ERR WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "RPUSH") == 0)
    {
        printf("DEBUG: Processing RPUSH\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'rpush' command\r\n");
        }
        else
        {
            if (rpush_command(db, tokens[1], tokens[2]))
            {
                int new_length = llen_command(db, tokens[1]);
                snprintf(response, sizeof(response), ":%d\r\n", new_length);
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "-ERR WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "LPOP") == 0)
    {
        printf("DEBUG: Processing LPOP\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'lpop' command\r\n");
        }
        else
        {
            char *value = lpop_command(db, tokens[1]);
            if (value)
            {
                snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(value), value);
                send_response_debug(client_socket, response);
                free(value);
            }
            else
            {
                send_response_debug(client_socket, "$-1\r\n"); // Redis nil response
            }
        }
    }
    else if (strcasecmp(tokens[0], "RPOP") == 0)
    {
        printf("DEBUG: Processing RPOP\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'rpop' command\r\n");
        }
        else
        {
            char *value = rpop_command(db, tokens[1]);
            if (value)
            {
                snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(value), value);
                send_response_debug(client_socket, response);
                free(value);
            }
            else
            {
                send_response_debug(client_socket, "$-1\r\n"); // Redis nil response
            }
        }
    }
    else if (strcasecmp(tokens[0], "LLEN") == 0)
    {
        printf("DEBUG: Processing LLEN\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'llen' command\r\n");
        }
        else
        {
            int length = llen_command(db, tokens[1]);
            snprintf(response, sizeof(response), ":%d\r\n", length);
            send_response_debug(client_socket, response);
        }
    }
    else if (strcasecmp(tokens[0], "LRANGE") == 0)
    {
        printf("DEBUG: Processing LRANGE\n");
        if (token_count != 4)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'lrange' command\r\n");
        }
        else
        {
            int start = atoi(tokens[2]);
            int stop = atoi(tokens[3]);
            int count = 0;
            char **elements = lrange_command(db, tokens[1], start, stop, &count);

            if (elements)
            {
                // Build RESP array response
                char *resp_buffer = malloc(8192); // Large buffer for response
                if (!resp_buffer)
                {
                    send_response_debug(client_socket, "-ERR Out of memory\r\n");
                    // Free the elements array
                    for (int i = 0; i < count; i++)
                    {
                        free(elements[i]);
                    }
                    free(elements);
                }
                else
                {
                    int offset = snprintf(resp_buffer, 8192, "*%d\r\n", count);

                    for (int i = 0; i < count && offset < 8000; i++)
                    {
                        offset += snprintf(resp_buffer + offset, 8192 - offset,
                                           "$%zu\r\n%s\r\n", strlen(elements[i]), elements[i]);
                    }

                    send_response_debug(client_socket, resp_buffer);
                    free(resp_buffer);

                    // Free the elements array
                    for (int i = 0; i < count; i++)
                    {
                        free(elements[i]);
                    }
                    free(elements);
                }
            }
            else
            {
                // Empty array
                send_response_debug(client_socket, "*0\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "HSET") == 0)
    {
        printf("DEBUG: Processing HSET\n");
        if (token_count != 4)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'hset' command\r\n");
        }
        else
        {
            if (hset_command(db, tokens[1], tokens[2], tokens[3]))
            {
                send_response_debug(client_socket, ":1\r\n"); // Redis returns 1 for new field, 0 for update
            }
            else
            {
                send_response_debug(client_socket, "-ERR WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "HGET") == 0)
    {
        printf("DEBUG: Processing HGET\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'hget' command\r\n");
        }
        else
        {
            char *value = hget_command(db, tokens[1], tokens[2]);
            if (value)
            {
                snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(value), value);
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "$-1\r\n"); // Redis nil response
            }
        }
    }
    else if (strcasecmp(tokens[0], "HDEL") == 0)
    {
        printf("DEBUG: Processing HDEL\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'hdel' command\r\n");
        }
        else
        {
            bool deleted = hdel_command(db, tokens[1], tokens[2]);
            send_response_debug(client_socket, deleted ? ":1\r\n" : ":0\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "HEXISTS") == 0)
    {
        printf("DEBUG: Processing HEXISTS\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'hexists' command\r\n");
        }
        else
        {
            bool exists = hexists_command(db, tokens[1], tokens[2]);
            send_response_debug(client_socket, exists ? ":1\r\n" : ":0\r\n");
        }
    }
    else if (strcasecmp(tokens[0], "HGETALL") == 0)
    {
        printf("DEBUG: Processing HGETALL\n");
        if (token_count != 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'hgetall' command\r\n");
        }
        else
        {
            int count = 0;
            char **fields_and_values = hgetall_command(db, tokens[1], &count);

            if (fields_and_values && count > 0)
            {
                // Build RESP array response
                char *resp_buffer = malloc(8192); // Large buffer for response
                if (!resp_buffer)
                {
                    send_response_debug(client_socket, "-ERR Out of memory\r\n");
                    // Free the array
                    for (int i = 0; i < count; i++)
                    {
                        free(fields_and_values[i]);
                    }
                    free(fields_and_values);
                }
                else
                {
                    int offset = snprintf(resp_buffer, 8192, "*%d\r\n", count);

                    for (int i = 0; i < count && offset < 8000; i++)
                    {
                        offset += snprintf(resp_buffer + offset, 8192 - offset,
                                           "$%zu\r\n%s\r\n", strlen(fields_and_values[i]), fields_and_values[i]);
                    }

                    send_response_debug(client_socket, resp_buffer);
                    free(resp_buffer);

                    // Free the array
                    for (int i = 0; i < count; i++)
                    {
                        free(fields_and_values[i]);
                    }
                    free(fields_and_values);
                }
            }
            else
            {
                // Empty array
                send_response_debug(client_socket, "*0\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "SUBSCRIBE") == 0)
    {
        printf("DEBUG: Processing SUBSCRIBE\n");
        if (token_count < 2)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'subscribe' command\r\n");
        }
        else
        {
            // Subscribe to all specified channels
            char response[4096];
            int offset = 0;

            for (int i = 1; i < token_count; i++)
            {
                bool success = subscribe_command(pubsub, client_socket, tokens[i]);
                if (success)
                {
                    // Send subscription confirmation (Redis format)
                    snprintf(response + offset, sizeof(response) - offset,
                             "*3\r\n$9\r\nsubscribe\r\n$%zu\r\n%s\r\n:%d\r\n",
                             strlen(tokens[i]), tokens[i], i);
                    offset = strlen(response);
                }
            }

            if (offset > 0)
            {
                send_response_debug(client_socket, response);
            }
            else
            {
                send_response_debug(client_socket, "-ERR Failed to subscribe to channels\r\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "UNSUBSCRIBE") == 0)
    {
        printf("DEBUG: Processing UNSUBSCRIBE\n");
        if (token_count == 1)
        {
            // Unsubscribe from all channels
            printf("DEBUG: Unsubscribing from all channels\n");

            // Get current subscriptions before unsubscribing
            int current_count = 0;
            char **current_channels = pubsub_get_subscribed_channels(pubsub, client_socket, &current_count);

            // Unsubscribe from all
            unsubscribe_all_command(pubsub, client_socket);

            // Send confirmation for each channel that was unsubscribed
            char response[8192] = {0};
            size_t offset = 0;

            for (int i = 0; i < current_count; i++)
            {
                if (current_channels && current_channels[i])
                {
                    int remaining = current_count - i - 1;
                    int written = snprintf(response + offset, sizeof(response) - offset,
                                           "*3\r\n$11\r\nunsubscribe\r\n$%zu\r\n%s\r\n:%d\r\n",
                                           strlen(current_channels[i]), current_channels[i], remaining);
                    if (written > 0 && offset + (size_t)written < sizeof(response))
                    {
                        offset += (size_t)written;
                    }
                    free(current_channels[i]);
                }
            }
            free(current_channels);

            // If no channels were subscribed to, send a single response
            if (current_count == 0)
            {
                snprintf(response, sizeof(response), "*3\r\n$11\r\nunsubscribe\r\n$-1\r\n:0\r\n");
            }

            send_response_debug(client_socket, response);
            printf("DEBUG: Sent unsubscribe-all response\n");
        }
        else
        {
            // Unsubscribe from specified channels
            printf("DEBUG: Unsubscribing from %d specific channels\n", token_count - 1);

            char response[8192] = {0};
            size_t offset = 0;
            int successful_unsubscribes = 0;

            for (int i = 1; i < token_count; i++)
            {
                printf("DEBUG: Attempting to unsubscribe from channel: %s\n", tokens[i]);

                // Check if client is currently subscribed to this channel
                bool was_subscribed = pubsub_is_subscribed(pubsub, client_socket, tokens[i]);

                if (was_subscribed)
                {
                    bool success = unsubscribe_command(pubsub, client_socket, tokens[i]);
                    printf("DEBUG: Unsubscribe from %s: %s\n", tokens[i], success ? "SUCCESS" : "FAILED");

                    if (success)
                    {
                        successful_unsubscribes++;

                        // Get remaining subscription count
                        int remaining_count = 0;
                        char **remaining_channels = pubsub_get_subscribed_channels(pubsub, client_socket, &remaining_count);

                        // Free the channels list (we only need the count)
                        if (remaining_channels)
                        {
                            for (int j = 0; j < remaining_count; j++)
                            {
                                free(remaining_channels[j]);
                            }
                            free(remaining_channels);
                        }

                        // Send unsubscription confirmation
                        int written = snprintf(response + offset, sizeof(response) - offset,
                                               "*3\r\n$11\r\nunsubscribe\r\n$%zu\r\n%s\r\n:%d\r\n",
                                               strlen(tokens[i]), tokens[i], remaining_count);

                        if (written > 0 && offset + (size_t)written < sizeof(response))
                        {
                            offset += (size_t)written;
                        }
                        else
                        {
                            printf("DEBUG: Response buffer full, sending partial response\n");
                            break;
                        }
                    }
                }
                else
                {
                    printf("DEBUG: Client was not subscribed to channel %s\n", tokens[i]);

                    // Still send a response for channels not subscribed to
                    // Get current subscription count
                    int current_count = 0;
                    char **current_channels = pubsub_get_subscribed_channels(pubsub, client_socket, &current_count);

                    // Free the channels list (we only need the count)
                    if (current_channels)
                    {
                        for (int j = 0; j < current_count; j++)
                        {
                            free(current_channels[j]);
                        }
                        free(current_channels);
                    }

                    int written = snprintf(response + offset, sizeof(response) - offset,
                                           "*3\r\n$11\r\nunsubscribe\r\n$%zu\r\n%s\r\n:%d\r\n",
                                           strlen(tokens[i]), tokens[i], current_count);

                    if (written > 0 && offset + (size_t)written < sizeof(response))
                    {
                        offset += (size_t)written;
                    }
                }
            }

            if (offset > 0)
            {
                send_response_debug(client_socket, response);
                printf("DEBUG: Sent unsubscribe response for %d channels\n", successful_unsubscribes);
            }
            else
            {
                send_response_debug(client_socket, "-ERR Failed to process unsubscribe request\r\n");
                printf("DEBUG: No response generated - this is unexpected\n");
            }
        }
    }
    else if (strcasecmp(tokens[0], "PUBLISH") == 0)
    {
        printf("DEBUG: Processing PUBLISH\n");
        if (token_count != 3)
        {
            send_response_debug(client_socket, "-ERR wrong number of arguments for 'publish' command\r\n");
        }
        else
        {
            int delivered = publish_command(pubsub, tokens[1], tokens[2]);
            snprintf(response, sizeof(response), ":%d\r\n", delivered);
            send_response_debug(client_socket, response);
        }
    }
    else if (strcasecmp(tokens[0], "PUBSUB") == 0)
    {
        printf("DEBUG: Processing PUBSUB\n");
        if (token_count >= 2 && strcasecmp(tokens[1], "CHANNELS") == 0)
        {
            // Return list of channels with at least one subscriber
            pthread_mutex_lock(&pubsub->mutex);

            char *resp_buffer = malloc(8192);
            if (!resp_buffer)
            {
                send_response_debug(client_socket, "-ERR Out of memory\r\n");
                pthread_mutex_unlock(&pubsub->mutex);
            }
            else
            {
                int channel_count = 0;
                int offset = 0;

                // First pass: count channels
                for (int i = 0; i < 1024; i++)
                {
                    Channel *channel = pubsub->channels[i];
                    while (channel)
                    {
                        if (channel->subscriber_count > 0)
                        {
                            channel_count++;
                        }
                        channel = channel->next;
                    }
                }

                // Build response
                offset = snprintf(resp_buffer, 8192, "*%d\r\n", channel_count);

                // Second pass: add channel names
                for (int i = 0; i < 1024 && offset < 8000; i++)
                {
                    Channel *channel = pubsub->channels[i];
                    while (channel && offset < 8000)
                    {
                        if (channel->subscriber_count > 0)
                        {
                            offset += snprintf(resp_buffer + offset, 8192 - offset,
                                               "$%zu\r\n%s\r\n", strlen(channel->name), channel->name);
                        }
                        channel = channel->next;
                    }
                }

                send_response_debug(client_socket, resp_buffer);
                free(resp_buffer);
            }

            pthread_mutex_unlock(&pubsub->mutex);
        }
        else if (token_count >= 3 && strcasecmp(tokens[1], "NUMSUB") == 0)
        {
            // Return number of subscribers for specified channels
            char *resp_buffer = malloc(8192);
            if (!resp_buffer)
            {
                send_response_debug(client_socket, "-ERR Out of memory\r\n");
            }
            else
            {
                int offset = snprintf(resp_buffer, 8192, "*%d\r\n", (token_count - 2) * 2);

                pthread_mutex_lock(&pubsub->mutex);

                for (int i = 2; i < token_count && offset < 8000; i++)
                {
                    const char *channel_name = tokens[i];
                    int subscriber_count = 0;

                    unsigned int index = pubsub_hash(channel_name);
                    Channel *channel = pubsub->channels[index];

                    while (channel)
                    {
                        if (strcmp(channel->name, channel_name) == 0)
                        {
                            subscriber_count = channel->subscriber_count;
                            break;
                        }
                        channel = channel->next;
                    }

                    offset += snprintf(resp_buffer + offset, 8192 - offset,
                                       "$%zu\r\n%s\r\n:%d\r\n",
                                       strlen(channel_name), channel_name, subscriber_count);
                }

                pthread_mutex_unlock(&pubsub->mutex);

                send_response_debug(client_socket, resp_buffer);
                free(resp_buffer);
            }
        }
        else
        {
            send_response_debug(client_socket, "-ERR Unknown PUBSUB subcommand\r\n");
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