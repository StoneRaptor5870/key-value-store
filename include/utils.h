#ifndef UTILS_H
#define UTILS_H

// Function  to tokenise a command string into an array of string
char **tokenise_command(const char *command, int *token_count);

// Function to free tokens
void free_tokens(char **tokens, int count);

#endif /* UTILS_H */