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

// EXPIRE command implementation - Set key to expire in N seconds
bool expire_command(Database *db, const char *key, int seconds)
{
    if (!db || !key || seconds < 0)
    {
        return false;
    }

    // Check if key exists first
    if (!db_exists(db, key))
        return false;

    // Calculate expiration time
    time_t expiration_time = time(NULL) + seconds;

    // Set the expiration
    db_set_expiration(db, key, expiration_time);

    return true;
}

int ttl_command(Database *db, const char *key)
{
    if (!db || !key)
        return -2; // Error case

    // Check if key exists
    if (!db_exists(db, key))
        return -2; // Key doesn't exist

    // Get expiration time
    time_t expiration = db_get_expiration(db, key);

    if (expiration == 0)
        return -1; // Key exists but has no expiration

    // Calculate remaining time
    time_t current_time = time(NULL);
    int ttl = (int)(expiration - current_time);

    // If TTL is negative or zero, the key has expired
    if (ttl <= 0)
    {
        // Clean up expired key
        db_delete(db, key);
        return -2; // Key has expired (doesn't exist anymore)
    }

    return ttl;
}

// PERSIST command implementation - Remove expiration from a key
bool persist_command(Database *db, const char *key)
{
    if (!db || !key)
        return false;

    // Check if key exists
    if (!db_exists(db, key))
        return false;

    // Remove expiration
    return db_remove_expiration(db, key);
}

// Display help information
void print_help()
{
    printf("Available commands:\n");
    printf("  SET key value       - Set key to hold string value\n");
    printf("  GET key             - Get the value of key\n");
    printf("  DEL key             - Delete key\n");
    printf("  EXISTS key          - Check if key exists\n");
    printf("  INCR key            - Increment the integer value of key by one\n");
    printf("  DECR key            - Decrement the integer value of key by one\n");
    printf("  EXPIRE key seconds  - Set key to expire in N seconds\n");
    printf("  TTL key             - Get remaining time to live for a key\n");
    printf("  PERSIST key         - Remove expiration from a key\n");
    printf("  SAVE filename       - Save the database to a file\n");
    printf("  LOAD filename       - Load the database from a file\n");
    printf("  HELP                - Show this help message\n");
    printf("  EXIT                - Exit the program\n");
    printf("\nServer options (when running in server mode):\n");
    printf("  INFO                - Get server information\n");
    printf("  PING                - Test connection (returns PONG)\n");
}