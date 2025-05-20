#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>

// Hash table size
#define HASH_TABLE_SIZE 1024

// Entry structure to store key-value pairs
typedef struct Entry
{
    char *key;
    char *value;
    struct Entry *next;
} Entry;

// Database structure
typedef struct
{
    Entry *hash_table[HASH_TABLE_SIZE];
} Database;

// Function prototypes for database operations
unsigned int hash(const char *key);
Database *db_create();
void db_free(Database *db);
void db_set(Database *db, const char *key, const char *value);
char *db_get(Database *db, const char *key);
bool db_exists(Database *db, const char *key);
bool db_delete(Database *db, const char *key);

#endif /* DATABASE_H */
