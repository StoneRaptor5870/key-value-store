#include "../include/database.h"
#include "../include/commands.h"
#include "../include/utils.h"
#include "../include/persistence.h"
#include "../include/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <strings.h>

void print_usage(const char *program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -p PORT     Specify server port (default: 8520)\n");
    printf("  -i          Interactive mode (CLI)\n");
    printf("  -f FILE     Load database from file at startup\n");
    printf("  -h          Display this help message\n");
}

// Main function
int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    bool interactive_mode = false;
    char *load_file = NULL;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:if:h")) != -1)
    {
        switch (opt)
        {
        case 'p':
            port = atoi(optarg);
            if (port <= 0 || port > 65535)
            {
                fprintf(stderr, "Invalid port number\n");
                return 1;
            }
            break;
        case 'i':
            interactive_mode = true;
            break;
        case 'f':
            load_file = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    // Create database
    Database *db = db_create();

    // Load database if specified
    if (load_file)
    {
        printf("Loading database from %s...\n", load_file);
        if (!load_command(db, load_file))
        {
            fprintf(stderr, "Failed to load database from %s\n", load_file);
        }
    }

    // Interactive CLI mode
    if (interactive_mode)
    {
        char command[1024];

        printf("KEY VALUE STORE (Type 'HELP' for commands)\n");

        while (1)
        {
            printf("key-value-store> ");

            // Get command from user
            if (fgets(command, sizeof(command), stdin) == NULL)
            {
                break;
            }

            // Remove trailing newline
            command[strcspn(command, "\n")] = '\0';

            // Check for exit command
            if (strcasecmp(command, "EXIT") == 0 || strcasecmp(command, "QUIT") == 0)
            {
                printf("Goodbye!\n");
                break;
            }

            // Check for help command
            if (strcasecmp(command, "HELP") == 0)
            {
                print_help();
                continue;
            }

            // Tokenise command
            int token_count = 0;
            char **tokens = tokenise_command(command, &token_count);

            if (token_count == 0)
            {
                continue;
            }

            // Process commands
            if (strcasecmp(tokens[0], "SET") == 0)
            {
                if (token_count != 3)
                {
                    printf("(error) Wrong number of arguments for 'SET' command\n");
                }
                else
                {
                    set_command(db, tokens[1], tokens[2]);
                    printf("OK\n");
                }
            }
            else if (strcasecmp(tokens[0], "GET") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'GET' command\n");
                }
                else
                {
                    char *value = get_command(db, tokens[1]);
                    if (value)
                    {
                        printf("\"%s\"\n", value);
                    }
                    else
                    {
                        printf("(nil)\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "DEL") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'DEL' command\n");
                }
                else
                {
                    if (del_command(db, tokens[1]))
                    {
                        printf("(integer) 1\n");
                    }
                    else
                    {
                        printf("(integer) 0\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "EXISTS") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'EXISTS' command\n");
                }
                else
                {
                    if (exists_command(db, tokens[1]))
                    {
                        printf("(integer) 1\n");
                    }
                    else
                    {
                        printf("(integer) 0\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "INCR") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'INCR' command\n");
                }
                else
                {
                    int new_value;
                    if (incr_command(db, tokens[1], &new_value))
                    {
                        printf("(integer) %d\n", new_value);
                    }
                    else
                    {
                        printf("(error) Value is not an integer or out of range\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "DECR") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'DECR' command\n");
                }
                else
                {
                    int new_value;
                    if (decr_command(db, tokens[1], &new_value))
                    {
                        printf("(integer) %d\n", new_value);
                    }
                    else
                    {
                        printf("(error) Value is not an integer or out of range\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "EXPIRE") == 0)
            {
                if (token_count != 3)
                {
                    printf("(error) Wrong number of arguments for 'EXPIRE' command\n");
                }
                else
                {
                    int seconds = atoi(tokens[2]);
                    if (seconds < 0)
                    {
                        printf("(error) Invalid expire time\n");
                    }
                    else
                    {
                        if (expire_command(db, tokens[1], seconds))
                        {
                            printf("(integer) 1\n");
                        }
                        else
                        {
                            printf("(integer) 0\n");
                        }
                    }
                }
            }
            else if (strcasecmp(tokens[0], "TTL") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'TTL' command\n");
                }
                else
                {
                    int ttl = ttl_command(db, tokens[1]);
                    printf("(integer) %d\n", ttl);
                }
            }
            else if (strcasecmp(tokens[0], "PERSIST") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'PERSIST' command\n");
                }
                else
                {
                    if (persist_command(db, tokens[1]))
                    {
                        printf("(integer) 1\n");
                    }
                    else
                    {
                        printf("(integer) 0\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "LPUSH") == 0)
            {
                if (token_count != 3)
                {
                    printf("(error) Wrong number of arguments for 'LPUSH' command\n");
                }
                else
                {
                    if (lpush_command(db, tokens[1], tokens[2]))
                    {
                        int len = llen_command(db, tokens[1]);
                        printf("(integer) %d\n", len);
                    }
                    else
                    {
                        printf("(error) Operation against a key holding the wrong kind of value\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "RPUSH") == 0)
            {
                if (token_count != 3)
                {
                    printf("(error) Wrong number of arguments for 'RPUSH' command\n");
                }
                else
                {
                    if (rpush_command(db, tokens[1], tokens[2]))
                    {
                        int len = llen_command(db, tokens[1]);
                        printf("(integer) %d\n", len);
                    }
                    else
                    {
                        printf("(error) Operation against a key holding the wrong kind of value\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "LPOP") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'LPOP' command\n");
                }
                else
                {
                    char *value = lpop_command(db, tokens[1]);
                    if (value)
                    {
                        printf("\"%s\"\n", value);
                        free(value);
                    }
                    else
                    {
                        printf("(nil)\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "RPOP") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'RPOP' command\n");
                }
                else
                {
                    char *value = rpop_command(db, tokens[1]);
                    if (value)
                    {
                        printf("\"%s\"\n", value);
                        free(value);
                    }
                    else
                    {
                        printf("(nil)\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "LLEN") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'LLEN' command\n");
                }
                else
                {
                    int len = llen_command(db, tokens[1]);
                    printf("(integer) %d\n", len);
                }
            }
            else if (strcasecmp(tokens[0], "LRANGE") == 0)
            {
                if (token_count != 4)
                {
                    printf("(error) Wrong number of arguments for 'LRANGE' command\n");
                }
                else
                {
                    int start = atoi(tokens[2]);
                    int stop = atoi(tokens[3]);
                    int count;
                    char **values = lrange_command(db, tokens[1], start, stop, &count);

                    if (values)
                    {
                        for (int i = 0; i < count; i++)
                        {
                            printf("%d) \"%s\"\n", i + 1, values[i]);
                            free(values[i]);
                        }
                        free(values);
                    }
                    else if (count == 0)
                    {
                        printf("(empty list or set)\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "SAVE") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'SAVE' command\n");
                }
                else
                {
                    if (save_command(db, tokens[1]))
                    {
                        printf("OK\n");
                    }
                    else
                    {
                        printf("(error) Failed to save database\n");
                    }
                }
            }
            else if (strcasecmp(tokens[0], "LOAD") == 0)
            {
                if (token_count != 2)
                {
                    printf("(error) Wrong number of arguments for 'LOAD' command\n");
                }
                else
                {
                    if (load_command(db, tokens[1]))
                    {
                        printf("OK\n");
                    }
                    else
                    {
                        printf("(error) Failed to load database\n");
                    }
                }
            }
            else
            {
                printf("(error) Unknown command '%s'\n", tokens[0]);
            }

            free_tokens(tokens, token_count);
        }
    }
    // Server mode (default)
    else
    {
        printf("Starting server on port %d\n", port);
        if (!start_server(db, port))
        {
            fprintf(stderr, "Failed to start server\n");
            db_free(db);
            return 1;
        }
    }

    db_free(db);
    return 0;
}