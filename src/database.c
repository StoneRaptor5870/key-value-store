#include "../include/database.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static char *my_strdup(const char *s)
{
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char *new_str = (char *)malloc(len);
    if (new_str)
    {
        memcpy(new_str, s, len);
    }
    return new_str;
}
unsigned int hash(const char *key)
{
    unsigned int hash_val = 0;
    while (*key)
    {
        hash_val = (hash_val << 5) + *key++;
    }
    return hash_val % HASH_TABLE_SIZE;
}

// Create a new database
Database *db_create()
{
    Database *db = (Database *)malloc(sizeof(Database));
    if (!db)
    {
        fprintf(stderr, "Failed to allocate memory for database\n");
        exit(EXIT_FAILURE);
    }

    // Initialize hash table
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        db->hash_table[i] = NULL;
    }

    return db;
}

// Free database resources
void db_free(Database *db)
{
    if (!db)
        return;

    // Free all entries
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        Entry *current = db->hash_table[i];
        while (current)
        {
            Entry *next = current->next;
            free(current->key);
            free(current->value);
            free(current);
            current = next;
        }
    }
    free(db);
}

// Set a key-value pair in the database
void db_set(Database *db, const char *key, const char *value)
{
    if (!db || !key || !value)
        return;

    unsigned int index = hash(key);

    // Check if the key already exists
    Entry *current = db->hash_table[index];
    while (current)
    {
        if (strcmp(current->key, key) == 0)
        {
            // Update existing entry
            char *new_value = my_strdup(value);
            if (!new_value)
            {
                fprintf(stderr, "Failed to allocate memory for value\n");
                return;
            }
            free(current->value);
            current->value = new_value;
            return;
        }
        current = current->next;
    }

    // Create new entry
    Entry *new_entry = (Entry *)malloc(sizeof(Entry));
    if (!new_entry)
    {
        fprintf(stderr, "Failed to allocate memory for entry\n");
        return;
    }

    new_entry->key = my_strdup(key);
    if (!new_entry->key)
    {
        fprintf(stderr, "Failed to allocate memory for key\n");
        free(new_entry);
        return;
    }

    new_entry->value = my_strdup(value);
    if (!new_entry->value)
    {
        fprintf(stderr, "Failed to allocate memory for value\n");
        free(new_entry->key);
        free(new_entry);
        return;
    }

    new_entry->expiration = 0; // No expiration by default
    new_entry->next = db->hash_table[index];
    db->hash_table[index] = new_entry;
}

// Get a value by key from the database
char *db_get(Database *db, const char *key)
{
    if (!db || !key)
        return NULL;

    unsigned int index = hash(key);

    Entry *current = db->hash_table[index];
    while (current)
    {
        if (strcmp(current->key, key) == 0)
        {
            // Check if entry is expired
            if (db_is_expired(current))
            {
                // Entry is expired, remove it and return NULL
                db_delete(db, key);
                return NULL;
            }
            return current->value;
        }
        current = current->next;
    }

    return NULL; // key not found
}

// Check if a key exists in the database
bool db_exists(Database *db, const char *key)
{
    return db_get(db, key) != NULL;
}

// Delete a key from the database
bool db_delete(Database *db, const char *key)
{
    if (!db || !key)
        return false;

    unsigned int index = hash(key);

    Entry *current = db->hash_table[index];
    Entry *prev = NULL;

    while (current)
    {
        if (strcmp(current->key, key) == 0)
        {
            // Remove entry
            if (prev)
            {
                prev->next = current->next;
            }
            else
            {
                db->hash_table[index] = current->next;
            }

            free(current->key);
            free(current->value);
            free(current);
            return true;
        }

        prev = current;
        current = current->next;
    }

    return false; // Key not found
}

// Check if an entry is expired
bool db_is_expired(Entry *entry)
{
    if (!entry || entry->expiration == 0)
    {
        return false;
    }

    return time(NULL) >= entry->expiration;
}

// Clean up expired entries from the database
void db_cleanup_expired(Database *db)
{
    if (!db)
        return;

    time_t current_time = time(NULL);

    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        Entry *current = db->hash_table[i];
        Entry *prev = NULL;

        while (current)
        {
            Entry *next = current->next;

            if (current->expiration != 0 && current_time >= current->expiration)
            {
                // Remove expired entry
                if (prev)
                {
                    prev->next = current->next;
                }
                else
                {
                    db->hash_table[i] = current->next;
                }

                free(current->key);
                free(current->value);
                free(current);
            }
            else
            {
                prev = current;
            }

            current = next;
        }
    }
}

// Set expiration time for a key
void db_set_expiration(Database *db, const char *key, time_t expiration)
{
    if (!db || !key)
        return;

    unsigned int index = hash(key);

    Entry *current = db->hash_table[index];

    while (current)
    {
        if (strcmp(current->key, key) == 0)
        {
            current->expiration = expiration;
            return;
        }
        current = current->next;
    }
}

// Get expiration time for a key
time_t db_get_expiration(Database *db, const char *key)
{
    if (!db || !key)
        return 0;

    unsigned int index = hash(key);

    Entry *current = db->hash_table[index];
    while (current)
    {
        if (strcmp(current->key, key) == 0)
        {
            return current->expiration;
        }
        current = current->next;
    }

    return 0; // Key not found
}

bool db_remove_expiration(Database *db, const char *key)
{
    if (!db || !key)
        return false;

    unsigned int index = hash(key);

    Entry *current = db->hash_table[index];
    while (current)
    {
        if (strcmp(current->key, key) == 0)
        {
            if (current->expiration != 0)
            {
                current->expiration = 0;
                return true;
            }
            return false; // Key exists but has no expiration
        }
        current = current->next;
    }

    return false; // Key not found
}