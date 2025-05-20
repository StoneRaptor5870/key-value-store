#define _POSIX_C_SOURCE 200809L
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

// Function to parse RESP (Redis Serialization Protocol) command
char *parse_resp(char *input, int *token_count)
{
    *token_count = 0;

    // Check if this is a RESP array message (starts with *)
    if (input[0] != '*')
    {
        // If not RESP, just return the original input
        return strdup(input);
    }

    // Parse array size
    int array_size = atoi(input + 1);
    if (array_size <= 0)
    {
        return NULL;
    }

    // Allocate memory for all parts
    char **parts = malloc(sizeof(char *) * array_size);
    if (!parts)
    {
        return NULL;
    }

    // Find the end of the first line
    char *line_end = strchr(input, '\r');
    if (!line_end || line_end[1] != '\n')
    {
        free(parts);
        return NULL;
    }

    char *current = line_end + 2; // Move past \r\n

    // Parse each bulk string in the array
    for (int i = 0; i < array_size; i++)
    {
        // Check if this is a bulk string
        if (current[0] != '$')
        {
            // Cleanup already allocated parts
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return NULL;
        }

        // Parse string length
        int str_len = atoi(current + 1);
        if (str_len < 0)
        {
            // Cleanup
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return NULL;
        }

        // Find the end of the length line
        line_end = strchr(current, '\r');
        if (!line_end || line_end[1] != '\n')
        {
            // Cleanup
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return NULL;
        }

        current = line_end + 2; // Move past \r\n

        // Allocate memory for the string and copy it
        parts[i] = malloc(str_len + 1);
        if (!parts[i])
        {
            // Cleanup
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return NULL;
        }

        memcpy(parts[i], current, str_len);
        parts[i][str_len] = '\0'; // Null-terminate the string

        current += str_len + 2; // Move past string and \r\n
    }

    // Convert the array of strings to a single space-separated command string
    size_t total_len = 0;
    for (int i = 0; i < array_size; i++)
    {
        total_len += strlen(parts[i]) + 1; // +1 for space
    }

    char *result = malloc(total_len);
    if (!result)
    {
        // Cleanup
        for (int i = 0; i < array_size; i++)
        {
            free(parts[i]);
        }
        free(parts);
        return NULL;
    }

    result[0] = '\0'; // Initialize empty string

    for (int i = 0; i < array_size; i++)
    {
        if (i > 0)
        {
            strcat(result, " ");
        }
        strcat(result, parts[i]);
    }

    // Set the token count to the array size
    *token_count = array_size;

    // Cleanup
    for (int i = 0; i < array_size; i++)
    {
        free(parts[i]);
    }
    free(parts);

    return result;
}