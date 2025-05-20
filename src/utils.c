#include "../include/utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>

// Free token array
void free_tokens(char **tokens, int count)
{
    if (!tokens)
        return;

    for (int i = 0; i < count; i++)
    {
        free(tokens[i]);
    }

    free(tokens);
}

// Tokenise command string into array of strings
char **tokenise_command(const char *command, int *token_count)
{
    *token_count = 0;

    // Count tokens
    bool in_token = false;
    bool in_quotes = false;
    const char *p = command;

    while (*p)
    {
        if (*p == '"')
        {
            in_quotes = !in_quotes;
        }

        if (!in_quotes && isspace(*p))
        {
            if (in_token)
            {
                in_token = false;
            }
        }
        else
        {
            if (!in_token)
            {
                in_token = true;
                (*token_count)++;
            }
        }
        p++;
    }

    // Allocate token array
    char **tokens = (char **)malloc(sizeof(char *) * (*token_count));
    if (!tokens)
    {
        fprintf(stderr, "Failed to allocate memory for tokens\n");
        exit(EXIT_FAILURE);
    }

    // Parse tokens
    in_token = false;
    in_quotes = false;
    p = command;
    int token_index = 0;
    const char *token_start = NULL;

    while (1)
    {
        if (*p == '"')
        {
            in_quotes = !in_quotes;
        }

        if (*p == '\0' || (!in_quotes && isspace(*p)))
        {
            if (in_token)
            {
                in_token = false;
                int token_len = p - token_start;
                if (token_start[0] == '"' && p[-1] == '"')
                {
                    // Remove quotes
                    token_start++;
                    token_len -= 2;
                }

                tokens[token_index] = (char *)malloc(token_len + 1);
                if (!tokens[token_index])
                {
                    fprintf(stderr, "Failed to allocate memory for token\n");
                    exit(EXIT_FAILURE);
                }

                strncpy(tokens[token_index], token_start, token_len);
                tokens[token_index][token_len] = '\0';
                token_index++;
            }

            if (*p == '\0')
            {
                break;
            }
        }
        else
        {
            if (!in_token)
            {
                in_token = true;
                token_start = p;
            }
        }
        p++;
    }

    return tokens;
}