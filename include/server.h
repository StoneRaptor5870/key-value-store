#ifndef SERVER_H
#define SERVER_H

#include "database.h"
#include "pubsub.h"
#include <stdbool.h>
#include <stddef.h>

// Default port for the server
#define DEFAULT_PORT 8520

// Function to start the TCP server
bool start_server(Database *db, int port);

// Function to handle client connection
void handle_client(int client_socket, Database *db, PubSubManager *pubsub);

// Function to process commands received from client
void process_client_command(int client_socket, Database *db, PubSubManager *pubsub, const char *command);

// Function to send response to client
void send_response_debug(int client_socket, const char *response);

#endif /* SERVER_H */