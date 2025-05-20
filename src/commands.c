#include "../include/commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SET command implementation
void set_command(Database *db, const char *key, const char *value)
{
    db_set(db, key, value);
}

// GET command implementation
char *get_command(Database *db, const char *key)
{
    return db_get(db, key);
}

// EXISTS command implementation
bool exists_command(Database *db, const char *key)
{
    return db_exists(db, key);
}

// DEL command implementation
bool del_command(Database *db, const char *key)
{
    return db_delete(db, key);
}

// INCR command implementation
bool incr_command(Database *db, const char *key, int *new_value)
{
    char *value = db_get(db, key);

    if (!value)
    {
        // Key doesn't exist, create with value 1
        char buffer[32];
        sprintf(buffer, "1");
        db_set(db, key, buffer);
        *new_value = 1;
        return true;
    }

    // Check if value is a valid integer
    char *endptr;
    long val = strtol(value, &endptr, 10);

    if (*endptr != '\0')
    {
        return false; // Not a valid integer
    }

    val++; // Increment

    // Store the new value
    char buffer[32];
    sprintf(buffer, "%ld", val);
    db_set(db, key, buffer);

    *new_value = (int)val;
    return true;
}

// DECR command implementation
bool decr_command(Database *db, const char *key, int *new_value)
{
    char *value = db_get(db, key);

    if (!value)
    {
        // Key doesn't exist, create with value -1
        char buffer[32];
        sprintf(buffer, "-1");
        db_set(db, key, buffer);
        *new_value = -1;
        return true;
    }

    // Check if value is a valid integer
    char *endptr;
    long val = strtol(value, &endptr, 10);

    if (*endptr != '\0')
    {
        return false; // Not a valid integer
    }

    val--; // Decrement

    // Store the new value
    char buffer[32];
    sprintf(buffer, "%ld", val);
    db_set(db, key, buffer);

    *new_value = (int)val;
    return true;
}

// Display help information
void print_help()
{
    printf("Available commands:\n");
    printf("  SET key value - Set key to hold the string value\n");
    printf("  GET key - Get the value of key\n");
    printf("  DEL key - Delete a key\n");
    printf("  EXISTS key - Check if a key exists\n");
    printf("  INCR key - Increment the integer value of a key\n");
    printf("  DECR key - Decrement the integer value of a key\n");
    printf("  SAVE filename - Save the database to a file\n");
    printf("  LOAD filename - Load the database from a file\n");
    printf("  HELP - Display this help\n");
    printf("  EXIT/QUIT - Exit the program\n");
}