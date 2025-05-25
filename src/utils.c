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

// Tokenise command string into array of strings (for regular commands, not RESP)
char **tokenise_command(const char *command, int *token_count)
{
    printf("DEBUG: tokenise_command called with: '%.100s'\n", command ? command : "NULL");

    *token_count = 0;

    if (!command || !*command)
    {
        printf("DEBUG: tokenise_command - empty command\n");
        return NULL;
    }

    // Count tokens
    bool in_token = false;
    bool in_quotes = false;
    const char *p = command;

    while (*p)
    {
        if (*p == '"' && (p == command || p[-1] != '\\'))
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

    printf("DEBUG: tokenise_command - found %d tokens\n", *token_count);

    if (*token_count == 0)
        return NULL;

    // Allocate token array
    char **tokens = (char **)malloc(sizeof(char *) * (*token_count));
    if (!tokens)
    {
        printf("DEBUG: tokenise_command - failed to allocate tokens array\n");
        *token_count = 0;
        return NULL;
    }

    // Initialize all pointers to NULL for safe cleanup
    for (int i = 0; i < *token_count; i++)
    {
        tokens[i] = NULL;
    }

    // Parse tokens
    in_token = false;
    in_quotes = false;
    p = command;
    int token_index = 0;
    const char *token_start = NULL;

    while (*p && token_index < *token_count)
    {
        if (*p == '"' && (p == command || p[-1] != '\\'))
        {
            in_quotes = !in_quotes;
        }

        if (!in_quotes && isspace(*p))
        {
            if (in_token)
            {
                in_token = false;
                int token_len = p - token_start;

                // Handle quoted strings
                if (token_start[0] == '"' && p > token_start && p[-1] == '"')
                {
                    token_start++;
                    token_len -= 2;
                }

                tokens[token_index] = (char *)malloc(token_len + 1);
                if (!tokens[token_index])
                {
                    printf("DEBUG: tokenise_command - failed to allocate token %d\n", token_index);
                    // Cleanup on failure
                    free_tokens(tokens, *token_count);
                    *token_count = 0;
                    return NULL;
                }

                strncpy(tokens[token_index], token_start, token_len);
                tokens[token_index][token_len] = '\0';

                printf("DEBUG: tokenise_command - token %d: '%s'\n", token_index, tokens[token_index]);
                token_index++;
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

    // Handle the last token if we're still in one
    if (in_token && token_index < *token_count)
    {
        int token_len = p - token_start;

        // Handle quoted strings
        if (token_start[0] == '"' && p > token_start && p[-1] == '"')
        {
            token_start++;
            token_len -= 2;
        }

        tokens[token_index] = (char *)malloc(token_len + 1);
        if (!tokens[token_index])
        {
            printf("DEBUG: tokenise_command - failed to allocate token %d\n", token_index);
            // Cleanup on failure
            free_tokens(tokens, *token_count);
            *token_count = 0;
            return NULL;
        }

        strncpy(tokens[token_index], token_start, token_len);
        tokens[token_index][token_len] = '\0';

        printf("DEBUG: tokenise_command - token %d: '%s'\n", token_index, tokens[token_index]);
        token_index++;
    }

    printf("DEBUG: tokenise_command - successfully parsed %d tokens\n", token_index);
    return tokens;
}

// Function to find first complete RESP command in a buffer
char *find_complete_resp_command(const char *buffer, size_t buffer_len, size_t *command_length)
{
    *command_length = 0;

    if (!buffer || buffer_len == 0)
        return NULL;

    // Handle inline commands (non-RESP) - look for CRLF
    if (buffer[0] != '*' && buffer[0] != '$' && buffer[0] != '+' &&
        buffer[0] != '-' && buffer[0] != ':')
    {
        for (size_t i = 0; i < buffer_len - 1; i++)
        {
            if (buffer[i] == '\r' && buffer[i + 1] == '\n')
            {
                *command_length = i + 2;
                return (char *)buffer;
            }
        }
        return NULL; // No complete command yet
    }

    // Handle RESP array commands
    if (buffer[0] == '*')
    {
        // Find first CRLF to get array size
        size_t first_crlf = 0;
        bool found_crlf = false;

        for (size_t i = 0; i < buffer_len - 1; i++)
        {
            if (buffer[i] == '\r' && buffer[i + 1] == '\n')
            {
                first_crlf = i;
                found_crlf = true;
                break;
            }
        }

        if (!found_crlf)
            return NULL;

        // Parse array size
        char size_str[16];
        size_t size_len = first_crlf - 1;
        if (size_len >= sizeof(size_str))
            return NULL; // Array size too long

        strncpy(size_str, buffer + 1, size_len);
        size_str[size_len] = '\0';

        int array_size = atoi(size_str);
        if (array_size <= 0)
            return NULL;

        size_t pos = first_crlf + 2; // Start after first CRLF

        // Process each element in the array
        for (int i = 0; i < array_size; i++)
        {
            if (pos >= buffer_len)
                return NULL; // Not enough data

            // Each element should be a bulk string starting with $
            if (buffer[pos] != '$')
                return NULL;

            // Find the CRLF after the length
            size_t length_end = 0;
            bool found_length_crlf = false;

            for (size_t j = pos; j < buffer_len - 1; j++)
            {
                if (buffer[j] == '\r' && buffer[j + 1] == '\n')
                {
                    length_end = j;
                    found_length_crlf = true;
                    break;
                }
            }

            if (!found_length_crlf)
                return NULL;

            // Parse string length
            char len_str[16];
            size_t len_str_len = length_end - pos - 1;
            if (len_str_len >= sizeof(len_str))
                return NULL;

            strncpy(len_str, buffer + pos + 1, len_str_len);
            len_str[len_str_len] = '\0';

            int str_len = atoi(len_str);
            if (str_len < 0)
                return NULL;

            pos = length_end + 2; // Move past length CRLF

            // Check if we have enough data for the string content + CRLF
            if (pos + str_len + 2 > buffer_len)
                return NULL;

            // Verify the string ends with CRLF
            if (buffer[pos + str_len] != '\r' || buffer[pos + str_len + 1] != '\n')
                return NULL;

            pos += str_len + 2; // Move past string content and CRLF
        }

        *command_length = pos;
        return (char *)buffer;
    }

    // Handle other RESP types (simple strings, errors, integers, bulk strings)
    for (size_t i = 0; i < buffer_len - 1; i++)
    {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n')
        {
            *command_length = i + 2;
            return (char *)buffer;
        }
    }

    return NULL;
}

// RESP protocol parsing
char **parse_resp_tokens(const char *input, size_t input_len, int *token_count)
{
    *token_count = 0;

    if (!input || input_len == 0)
        return NULL;

    printf("DEBUG: parse_resp_tokens - Raw input (%zu bytes): ", input_len);
    for (size_t i = 0; i < input_len && i < 100; i++)
    {
        if (input[i] >= 32 && input[i] <= 126)
        {
            printf("%c", input[i]);
        }
        else
        {
            printf("\\x%02x", (unsigned char)input[i]);
        }
    }
    printf("%s\n", (input_len > 100) ? "..." : "");

    // Handle inline commands (non-RESP) - need to tokenize these
    if (input[0] != '*')
    {
        printf("DEBUG: parse_resp_tokens - Processing as inline command\n");
        // Find the end of the command (CRLF or end of string)
        size_t cmd_len = 0;
        for (size_t i = 0; i < input_len; i++)
        {
            if (input[i] == '\r' || input[i] == '\n' || input[i] == '\0')
            {
                cmd_len = i;
                break;
            }
        }
        if (cmd_len == 0)
            cmd_len = input_len;

        char *cmd_str = malloc(cmd_len + 1);
        if (!cmd_str)
            return NULL;

        strncpy(cmd_str, input, cmd_len);
        cmd_str[cmd_len] = '\0';

        printf("DEBUG: parse_resp_tokens - Inline command: '%s'\n", cmd_str);

        // For inline commands, we need to tokenize manually
        char **tokens = tokenise_command(cmd_str, token_count);
        free(cmd_str);
        return tokens;
    }

    printf("DEBUG: parse_resp_tokens - Processing as RESP array\n");

    // Parse RESP array
    // Find first CRLF
    size_t first_crlf = 0;
    for (size_t i = 0; i < input_len - 1; i++)
    {
        if (input[i] == '\r' && input[i + 1] == '\n')
        {
            first_crlf = i;
            break;
        }
    }

    if (first_crlf == 0)
    {
        printf("DEBUG: parse_resp_tokens - No CRLF found after array size\n");
        return NULL;
    }

    // Parse array size
    int array_size = atoi(input + 1);
    printf("DEBUG: parse_resp_tokens - Array size: %d\n", array_size);

    if (array_size <= 0)
        return NULL;

    // Allocate array to store parsed elements (this will be our final result)
    char **tokens = malloc(sizeof(char *) * array_size);
    if (!tokens)
        return NULL;

    // Initialize all pointers for safe cleanup
    for (int i = 0; i < array_size; i++)
    {
        tokens[i] = NULL;
    }

    size_t pos = first_crlf + 2;
    bool parse_error = false;

    // Parse each bulk string directly into tokens array
    for (int i = 0; i < array_size && !parse_error; i++)
    {
        printf("DEBUG: parse_resp_tokens - Processing element %d at position %zu\n", i, pos);

        if (pos >= input_len || input[pos] != '$')
        {
            printf("DEBUG: parse_resp_tokens - Expected '$' at position %zu, found '%c'\n", pos, (pos < input_len) ? input[pos] : '?');
            parse_error = true;
            break;
        }

        // Find length CRLF
        size_t length_crlf = 0;
        bool found = false;
        for (size_t j = pos; j < input_len - 1; j++)
        {
            if (input[j] == '\r' && input[j + 1] == '\n')
            {
                length_crlf = j;
                found = true;
                break;
            }
        }

        if (!found)
        {
            printf("DEBUG: parse_resp_tokens - No CRLF found after length for element %d\n", i);
            parse_error = true;
            break;
        }

        int str_len = atoi(input + pos + 1);
        printf("DEBUG: parse_resp_tokens - Element %d length: %d\n", i, str_len);

        if (str_len < 0)
        {
            parse_error = true;
            break;
        }

        pos = length_crlf + 2;

        if (pos + str_len + 2 > input_len)
        {
            printf("DEBUG: parse_resp_tokens - Not enough data for element %d (need %d more bytes)\n", i, (int)(pos + str_len + 2 - input_len));
            parse_error = true;
            break;
        }

        tokens[i] = malloc(str_len + 1);
        if (!tokens[i])
        {
            parse_error = true;
            break;
        }

        memcpy(tokens[i], input + pos, str_len);
        tokens[i][str_len] = '\0';

        printf("DEBUG: parse_resp_tokens - Token %d: '%s'\n", i, tokens[i]);

        pos += str_len + 2;
    }

    // Handle parsing errors - cleanup
    if (parse_error)
    {
        printf("DEBUG: parse_resp_tokens - Parse error occurred\n");
        for (int i = 0; i < array_size; i++)
        {
            free(tokens[i]);
        }
        free(tokens);
        return NULL;
    }

    *token_count = array_size;
    printf("DEBUG: parse_resp_tokens - Successfully parsed %d tokens\n", array_size);

    return tokens;
}