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

// Function to find first complete RESP command in a buffer
char *find_complete_resp_command(const char *buffer, size_t *command_length)
{
    *command_length = 0;

    if (!buffer || !*buffer)
    {
        return NULL;
    }

    // If not a RESP array, look for CRLF as terminator
    if (buffer[0] != '*')
    {
        char *crlf = strstr(buffer, "\r\n");
        if (!crlf)
        {
            return NULL; // No complete command yet
        }
        *command_length = (crlf - buffer) + 2;
        return (char *)buffer;
    }

    // Parse array size
    int array_size = atoi(buffer + 1);
    if (array_size <= 0)
    {
        // Invalid array size, treat as non-RESP
        char *crlf = strstr(buffer, "\r\n");
        if (!crlf)
        {
            return NULL;
        }
        *command_length = (crlf - buffer) + 2;
        return (char *)buffer;
    }

    // Find the end of the array size line
    char *current = strstr(buffer, "\r\n");
    if (!current)
    {
        return NULL;
    }
    current += 2; // Move past \r\n

    // Process each bulk string in the array
    for (int i = 0; i < array_size; i++)
    {
        // Check if we have enough data
        if (*current == '\0')
        {
            return NULL; // Incomplete command
        }

        // Check if this is a bulk string
        if (current[0] != '$')
        {
            return NULL; // Invalid format
        }

        // Find the end of the length line
        char *line_end = strstr(current, "\r\n");
        if (!line_end)
        {
            return NULL; // Incomplete command
        }

        // Parse string length
        int str_len = atoi(current + 1);
        if (str_len < 0)
        {
            return NULL; // Invalid string length
        }

        current = line_end + 2; // Move past \r\n

        // Make sure we have the full string data
        if (strlen(current) < (size_t)(str_len + 2))
        {
            return NULL; // Not enough data for string content
        }

        current += str_len + 2; // Move past string content and \r\n
    }

    // We've processed the entire command
    *command_length = current - buffer;
    return (char *)buffer;
}

// RESP protocol parsing
char *parse_resp(char *input, int *token_count)
{
    *token_count = 0;

    // Check if input is empty
    if (!input || !*input)
    {
        printf("RESP parser: Empty input\n");
        return strdup("");
    }

    // Print input for debugging
    printf("RESP parser input: '%.*s%s'\n",
           (int)(strlen(input) > 50 ? 50 : strlen(input)),
           input,
           strlen(input) > 50 ? "...'" : "'");

    // Check if this is a RESP array message (starts with *)
    if (input[0] != '*')
    {
        printf("RESP parser: Not a RESP array, using as-is\n");
        return strdup(input);
    }

    // Parse array size
    int array_size = atoi(input + 1);
    if (array_size <= 0)
    {
        printf("RESP parser: Invalid array size: %d\n", array_size);
        return strdup(input); // Return original if invalid array size
    }

    printf("RESP parser: Array size: %d\n", array_size);

    // Allocate memory for all parts
    char **parts = malloc(sizeof(char *) * array_size);
    if (!parts)
    {
        printf("RESP parser: Memory allocation failed\n");
        return strdup(input); // Return original if memory allocation fails
    }

    // Find the end of the first line
    char *line_end = strstr(input, "\r\n");
    if (!line_end)
    {
        printf("RESP parser: No CRLF found in initial array marker\n");
        free(parts);
        return strdup(input); // Return original if format is wrong
    }

    char *current = line_end + 2; // Move past \r\n

    // Parse each bulk string in the array
    for (int i = 0; i < array_size; i++)
    {
        // Check if we have enough data
        if (*current == '\0')
        {
            printf("RESP parser: Unexpected end of input at item %d\n", i);
            // Cleanup already allocated parts
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return strdup(input);
        }

        // Check if this is a bulk string
        if (current[0] != '$')
        {
            printf("RESP parser: Expected bulk string '$' at item %d, got '%c'\n", i, current[0]);
            // Cleanup already allocated parts
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return strdup(input); // Return original if format is wrong
        }

        // Parse string length
        int str_len = atoi(current + 1);
        if (str_len < 0)
        {
            printf("RESP parser: Invalid string length: %d at item %d\n", str_len, i);
            // Cleanup
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return strdup(input); // Return original if invalid length
        }

        printf("RESP parser: Item %d has length %d\n", i, str_len);

        // Find the end of the length line
        line_end = strstr(current, "\r\n");
        if (!line_end)
        {
            printf("RESP parser: No CRLF found after string length at item %d\n", i);
            // Cleanup
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return strdup(input); // Return original if format is wrong
        }

        current = line_end + 2; // Move past \r\n

        // Allocate memory for the string and copy it
        parts[i] = malloc(str_len + 1);
        if (!parts[i])
        {
            printf("RESP parser: Memory allocation failed for item %d\n", i);
            // Cleanup
            for (int j = 0; j < i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return strdup(input); // Return original if memory allocation fails
        }

        // Check if we have enough data left
        if (strlen(current) < (size_t)(str_len + 2))
        {
            printf("RESP parser: Not enough data for string content at item %d\n", i);
            // Cleanup
            for (int j = 0; j <= i; j++)
            {
                free(parts[j]);
            }
            free(parts);
            return strdup(input); // Return original if not enough data
        }

        memcpy(parts[i], current, str_len);
        parts[i][str_len] = '\0'; // Null-terminate the string

        printf("RESP parser: Item %d content: '%s'\n", i, parts[i]);

        current += str_len + 2; // Move past string and \r\n
    }

    // Convert the array of strings to a single space-separated command string
    size_t total_len = 0;
    for (int i = 0; i < array_size; i++)
    {
        total_len += strlen(parts[i]) + 1; // +1 for space or null terminator
    }

    char *result = malloc(total_len);
    if (!result)
    {
        printf("RESP parser: Memory allocation failed for result\n");
        // Cleanup
        for (int i = 0; i < array_size; i++)
        {
            free(parts[i]);
        }
        free(parts);
        return strdup(input); // Return original if memory allocation fails
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

    printf("RESP parser: Final result: '%s'\n", result);

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