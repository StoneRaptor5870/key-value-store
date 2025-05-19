#include "../include/database.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Simple hash function
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
    if (!db) {
        fprintf(stderr, "Failed to allocate memory for database\n");
        exit(EXIT_FAILURE);
    }

    // Initialize hash table
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        db -> hash_table[i] = NULL;
    }

    return db;
}

// Free database resources
void db_free(Database* db) {
    if (!db) return;

    // Free all entries
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        Entry* current = db -> hash_table[i];
        while (current) {
            Entry* next = current -> next;
            free(current -> key);
            free(current -> value);
            free(current);
            current = next;
        }
    }
    free(db);
}

// Set a key-value pair in the database
void db_set(Database* db, const char* key, const char* value) {
    unsigned int index = hash(key);

    // Check if the key already exits
    Entry* current = db -> hash_table[index];
    while (current)
    {
        if(strcmp(current->key, key) == 0) {
            free(current->value);
            current->value = strdup(value);
            return;
        }
        current = current->next;
    }

    // Create new entry
    Entry* new_entry = (Entry*)malloc(sizeof(Entry));

    if(!new_entry) {
        fprintf(stderr, "Failed to allocate memory for entry\n");
        return;
    }

    new_entry->key = strdup(key);
    new_entry->value = strdup(value);
    new_entry->next = db->hash_table[index];
    db->hash_table[index] = new_entry;
}