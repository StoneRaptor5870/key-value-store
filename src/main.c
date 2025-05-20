#include "../include/database.h"
#include "../include/commands.h"
#include "../include/utils.h"
#include "../include/persistence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// Main function
int main()
{
    Database *db = db_create();
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

    db_free(db);
    return 0;
}