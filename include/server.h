#ifndef SERVER_H
#define SERVER_H

#include "database.h"
#include <stdbool.h>

// Default port for the server
#define DEFAULT_PORT 8520

// Function to start the TCP server
bool start_server(Database *db, int port);

// Function to handle client connection
void handle_client(int client_socket, Database *db);

// Function to process commands received from client
void process_client_command(int client_socket, Database *db, const char *command);

#endif /* SERVER_H */