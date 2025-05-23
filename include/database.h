#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <time.h>

// Hash table size
#define HASH_TABLE_SIZE 1024

// Entry structure to store key-value pairs
typedef struct Entry
{
    char *key;
    char *value;
    time_t expiration; // (0 = no expiration)
    struct Entry *next;
} Entry;

// Database structure
typedef struct
{
    Entry *hash_table[HASH_TABLE_SIZE];
} Database;

// Hash function
unsigned int hash(const char *key);

// Function prototypes for database operations
Database *db_create();
void db_free(Database *db);
void db_set(Database *db, const char *key, const char *value);
char *db_get(Database *db, const char *key);
bool db_exists(Database *db, const char *key);
bool db_delete(Database *db, const char *key);

// TTL-related function prototypes
void db_set_expiration(Database *db, const char *key, time_t expiration);
time_t db_get_expiration(Database *db, const char *key);
bool db_remove_expiration(Database *db, const char *key);
void db_cleanup_expired(Database *db);
bool db_is_expired(Entry *entry);

#endif /* DATABASE_H */
