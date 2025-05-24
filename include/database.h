#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <time.h>

// Hash table size
#define HASH_TABLE_SIZE 1024

// Value type
typedef enum
{
    VALUE_STRING,
    VALUE_LIST,
    VALUE_HASH
} ValueType;

// Hash structure
typedef struct HashField
{
    char *field;
    char *value;
    struct HashField *next;
} HashField;

typedef struct
{
    HashField **buckets;
    size_t bucket_count;
    size_t field_count;
} Hash;

// List node structure for doubly linked list
typedef struct ListNode
{
    char *data;
    struct ListNode *prev;
    struct ListNode *next;
} ListNode;

// List Structure
typedef struct
{
    ListNode *head;
    ListNode *tail;
    size_t length;
} List;

// Entry structure to store key-value pairs for different value types
typedef struct Entry
{
    char *key;
    ValueType type;
    union
    {
        char *string_value;
        List *list_value;
        Hash *hash_value;
    } value;
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

// Database functions
Database *db_create();
void db_free(Database *db);
void db_cleanup_expired(Database *db);
bool db_is_expired(Entry *entry);

// Function prototypes for string operations
void db_set(Database *db, const char *key, const char *value);
char *db_get(Database *db, const char *key);
bool db_exists(Database *db, const char *key);
bool db_delete(Database *db, const char *key);

// TTL-related function prototypes
void db_set_expiration(Database *db, const char *key, time_t expiration);
time_t db_get_expiration(Database *db, const char *key);
bool db_remove_expiration(Database *db, const char *key);

// Function prototypes for list operations
bool db_lpush(Database *db, const char *key, const char *value);
bool db_rpush(Database *db, const char *key, const char *value);
char *db_lpop(Database *db, const char *key);
char *db_rpop(Database *db, const char *key);
char **db_lrange(Database *db, const char *key, int start, int stop, int *count);
int db_llen(Database *db, const char *key);
List *create_list(void);
void free_list_node(ListNode *node);
void free_list(List *list);

// Function prototypes for hash operations
Hash *create_hash();
void free_hash(Hash *hash);
bool db_hset(Database *db, const char *key, const char *field, const char *value);
char *db_hget(Database *db, const char *key, const char *field);
char **db_hgetall(Database *db, const char *key, int *count);
bool db_hdel(Database *db, const char *key, const char *field);
bool db_hexists(Database *db, const char *key, const char *field);

#endif /* DATABASE_H */
