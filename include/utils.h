#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

// Function to tokenise a command string into an array of strings
char **tokenise_command(const char *command, int *token_count);

// Function to free tokens
void free_tokens(char **tokens, int count);

// Function to parse RESP (Redis Serialization Protocol) command
char **parse_resp_tokens(const char *input, size_t input_len, int *token_count);

// Function to find complete RESP command in buffer
char *find_complete_resp_command(const char *buffer, size_t buffer_len, size_t *command_length);

#endif /* UTILS_H */