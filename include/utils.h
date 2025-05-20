#ifndef UTILS_H
#define UTILS_H

// Function  to tokenise a command string into an array of string
char **tokenise_command(const char *command, int *token_count);

// Function to free tokens
void free_tokens(char **tokens, int count);

// Function to parse RESP (Redis Serialization Protocol) command
char *parse_resp(char *input, int *token_count);

#endif /* UTILS_H */