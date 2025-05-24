#include "../include/database.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define HASH_BUCKET_SIZE 16 // Small hash table for fields within a hash

// Forward declarations for static functions
static char *my_strdup(const char *s);
static ListNode *create_list_node(const char *data);
static Entry *get_entry(Database *db, const char *key);

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

            // Free value based on type
            if (current->type == VALUE_STRING)
            {
                free(current->value.string_value);
            }
            else if (current->type == VALUE_LIST)
            {
                free_list(current->value.list_value);
            }
            else if (current->type == VALUE_HASH)
            {
                free_hash(current->value.hash_value);
            }

            free(current);
            current = next;
        }
    }
    free(db);
}

// Get entry by key
static Entry *get_entry(Database *db, const char *key)
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
                db_delete(db, key);
                return NULL;
            }
            return current;
        }
        current = current->next;
    }
    return NULL;
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
            // Free existing value based on type
            if (current->type == VALUE_STRING)
            {
                free(current->value.string_value);
            }
            else if (current->type == VALUE_LIST)
            {
                free(current->value.list_value);
            }
            else if (current->type == VALUE_HASH)
            {
                free_hash(current->value.hash_value);
            }

            // Update with new string value
            current->type = VALUE_STRING;
            current->value.string_value = my_strdup(value);
            if (!current->value.string_value)
            {
                fprintf(stderr, "Failed to allocate memory for value\n");
                return;
            }
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

    new_entry->type = VALUE_STRING;
    new_entry->value.string_value = my_strdup(value);
    if (!new_entry->value.string_value)
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
    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_STRING)
        return NULL;

    return entry->value.string_value;
}

// Check if a key exists in the database
bool db_exists(Database *db, const char *key)
{
    return get_entry(db, key) != NULL;
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

            if (current->type == VALUE_STRING)
            {
                free(current->value.string_value);
            }
            else if (current->type == VALUE_LIST)
            {
                free_list(current->value.list_value);
            }
            else if (current->type == VALUE_HASH)
            {
                free_hash(current->value.hash_value);
            }

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

                if (current->type == VALUE_STRING)
                {
                    free(current->value.string_value);
                }
                else if (current->type == VALUE_LIST)
                {
                    free_list(current->value.list_value);
                }

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

static ListNode *create_list_node(const char *data)
{
    ListNode *node = (ListNode *)malloc(sizeof(ListNode));
    if (!node)
        return NULL;

    node->data = my_strdup(data);
    if (!node->data)
    {
        free(node);
        return NULL;
    }

    node->prev = NULL;
    node->next = NULL;
    return node;
}

void free_list_node(ListNode *node)
{
    if (node)
    {
        free(node->data);
        free(node);
    }
}

List *create_list()
{
    List *list = (List *)malloc(sizeof(List));
    if (!list)
        return NULL;

    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
    return list;
}

void free_list(List *list)
{
    if (!list)
        return;

    ListNode *current = list->head;
    while (current)
    {
        ListNode *next = current->next;
        free_list_node(current);
        current = next;
    }

    free(list);
}

bool db_lpush(Database *db, const char *key, const char *value)
{
    if (!db || !key || !value)
        return false;

    unsigned int index = hash(key);
    Entry *entry = get_entry(db, key);

    if (entry)
    {
        // Key exists, must be a list
        if (entry->type != VALUE_LIST)
            return false; // Type mismatch
    }
    else
    {
        // Create new list entry
        entry = (Entry *)malloc(sizeof(Entry));
        if (!entry)
            return false;

        entry->key = my_strdup(key);
        if (!entry->key)
        {
            free(entry);
            return false;
        }

        entry->type = VALUE_LIST;
        entry->value.list_value = create_list();
        if (!entry->value.list_value)
        {
            free(entry->key);
            free(entry);
            return false;
        }

        entry->expiration = 0;
        entry->next = db->hash_table[index];
        db->hash_table[index] = entry;
    }

    // Add to the left of list
    ListNode *new_node = create_list_node(value);
    if (!new_node)
        return false;

    List *list = entry->value.list_value;

    if (list->head == NULL)
    {
        // Empty list
        list->head = list->tail = new_node;
    }
    else
    {
        // Add to beginning
        new_node->next = list->head;
        list->head->prev = new_node;
        list->head = new_node;
    }

    list->length++;
    return true;
}

bool db_rpush(Database *db, const char *key, const char *value)
{
    if (!db || !key || !value)
        return false;

    unsigned int index = hash(key);
    Entry *entry = get_entry(db, key);

    if (entry)
    {
        // Key exists, must be a list
        if (entry->type != VALUE_LIST)
            return false; // Type mismatch
    }
    else
    {
        // Create new list entry
        entry = (Entry *)malloc(sizeof(Entry));
        if (!entry)
            return false;

        entry->key = my_strdup(key);
        if (!entry->key)
        {
            free(entry);
            return false;
        }

        entry->type = VALUE_LIST;
        entry->value.list_value = create_list();
        if (!entry->value.list_value)
        {
            free(entry->key);
            free(entry);
            return false;
        }

        entry->expiration = 0;
        entry->next = db->hash_table[index];
        db->hash_table[index] = entry;
    }

    // Add to right of list
    ListNode *new_node = create_list_node(value);
    if (!new_node)
        return false;

    List *list = entry->value.list_value;

    if (list->tail == NULL)
    {
        // Empty list
        list->head = list->tail = new_node;
    }
    else
    {
        // Add to end
        new_node->prev = list->tail;
        list->tail->next = new_node;
        list->tail = new_node;
    }

    list->length++;
    return true;
}

char *db_lpop(Database *db, const char *key)
{
    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_LIST)
        return NULL;

    List *list = entry->value.list_value;
    if (list->length == 0)
        return NULL;

    ListNode *node = list->head;
    char *data = my_strdup(node->data);

    // Remove from list
    if (list->length == 1)
    {
        list->head = list->tail = NULL;
    }
    else
    {
        list->head = node->next;
        list->head->prev = NULL;
    }

    list->length--;
    free_list_node(node);

    // If list is empty, remove the key
    if (list->length == 0)
    {
        db_delete(db, key);
    }

    return data;
}

char *db_rpop(Database *db, const char *key)
{
    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_LIST)
        return NULL;

    List *list = entry->value.list_value;
    if (list->length == 0)
        return NULL;

    ListNode *node = list->tail;
    char *data = my_strdup(node->data);

    // Remove from list
    if (list->length == 1)
    {
        list->head = list->tail = NULL;
    }
    else
    {
        list->tail = node->prev;
        list->tail->next = NULL;
    }

    list->length--;
    free_list_node(node);

    // If list is empty, remove the key
    if (list->length == 0)
    {
        db_delete(db, key);
    }

    return data;
}

int db_llen(Database *db, const char *key)
{
    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_LIST)
        return 0;

    return (int)entry->value.list_value->length;
}

char **db_lrange(Database *db, const char *key, int start, int stop, int *count)
{
    *count = 0;
    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_LIST)
        return NULL;

    List *list = entry->value.list_value;
    int len = (int)list->length;

    if (len == 0)
        return NULL;

    // Handle negative indices
    if (start < 0)
        start = len + start;
    if (stop < 0)
        stop = len + stop;

    // Clamp to valid range
    if (start < 0)
        start = 0;
    if (stop >= len)
        stop = len - 1;
    if (start > stop)
        return NULL;

    int result_count = stop - start + 1;
    char **result = (char **)malloc(result_count * sizeof(char *));
    if (!result)
        return NULL;

    // Navigate to start position
    ListNode *current = list->head;
    for (int i = 0; i < start && current; i++)
    {
        current = current->next;
    }

    // Collect elements
    int idx = 0;
    for (int i = start; i <= stop && current; i++)
    {
        result[idx] = my_strdup(current->data);
        if (!result[idx])
        {
            // Cleanup on error
            for (int j = 0; j < idx; j++)
            {
                free(result[j]);
            }
            free(result);
            return NULL;
        }
        idx++;
        current = current->next;
    }

    *count = result_count;
    return result;
}

// ----------------------------------Hash operations logic-----------------------------------

// Hash function for hash fields
static unsigned int hash_field(const char *field)
{
    unsigned int hash_val = 0;
    while (*field)
    {
        hash_val = (hash_val << 5) + *field++;
    }
    return hash_val % HASH_BUCKET_SIZE;
}

// Create a new hash structure
Hash *create_hash()
{
    Hash *hash = (Hash *)malloc(sizeof(Hash));
    if (!hash)
        return NULL;

    hash->buckets = (HashField **)calloc(HASH_BUCKET_SIZE, sizeof(HashField *));
    if (!hash->buckets)
    {
        free(hash);
        return NULL;
    }

    hash->bucket_count = HASH_BUCKET_SIZE;
    hash->field_count = 0;
    return hash;
}

// Free hash structure and all its fields
void free_hash(Hash *hash)
{
    if (!hash)
        return;

    for (size_t i = 0; i < hash->bucket_count; i++)
    {
        HashField *current = hash->buckets[i];
        while (current)
        {
            HashField *next = current->next;
            free(current->field);
            free(current->value);
            free(current);
            current = next;
        }
    }

    free(hash->buckets);
    free(hash);
}

// HSET - Set field in hash stored at key
bool db_hset(Database *db, const char *key, const char *field, const char *value)
{
    if (!db || !key || !field || !value)
        return false;

    unsigned int index = hash(key);
    Entry *entry = get_entry(db, key);

    if (entry)
    {
        // Key exists, must be a hash
        if (entry->type != VALUE_HASH)
            return false; // Type mismatch
    }
    else
    {
        // Create a new hash entry
        entry = (Entry *)malloc(sizeof(Entry));
        if (!entry)
            return false;

        entry->key = my_strdup(key);
        if (!entry->key)
        {
            free(entry);
            return false;
        }

        entry->type = VALUE_HASH;
        entry->value.hash_value = create_hash();
        if (!entry->value.hash_value)
        {
            free(entry->key);
            free(entry);
            return false;
        }

        entry->expiration = 0;
        entry->next = db->hash_table[index];
        db->hash_table[index] = entry;
    }

    Hash *hash = entry->value.hash_value;
    unsigned int field_index = hash_field(field);

    // Check if field already exists
    HashField *current = hash->buckets[field_index];
    while (current)
    {
        if (strcmp(current->field, field) == 0)
        {
            // Field exists, update value
            free(current->value);
            current->value = my_strdup(value);
            return current->value != NULL;
        }
        current = current->next;
    }

    // Field doesn't exist, create new one
    HashField *new_field = (HashField *)malloc(sizeof(HashField));
    if (!new_field)
        return false;

    new_field->field = my_strdup(field);
    new_field->value = my_strdup(value);
    if (!new_field->field || !new_field->value)
    {
        free(new_field->field);
        free(new_field->value);
        free(new_field);
        return false;
    }

    new_field->next = hash->buckets[field_index];
    hash->buckets[field_index] = new_field;
    hash->field_count++;

    return true;
}

// HGET - Get value of field in hash stored at a key
char *db_hget(Database *db, const char *key, const char *field)
{
    if (!db || !key || !field)
        return NULL;

    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_HASH)
        return NULL;

    Hash *hash = entry->value.hash_value;
    unsigned int field_index = hash_field(field);

    HashField *current = hash->buckets[field_index];
    while (current)
    {
        if (strcmp(current->field, field) == 0)
        {
            return current->value;
        }
        current = current->next;
    }

    return NULL; // Field not found
}

// HEXISTS - Check if the field exists in hash
bool db_hexists(Database *db, const char *key, const char *field)
{
    if (!db || !key || !field)
        return false;

    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_HASH)
        return false;

    Hash *hash = entry->value.hash_value;
    unsigned int field_index = hash_field(field);

    HashField *current = hash->buckets[field_index];
    while (current)
    {
        if (strcmp(current->field, field) == 0)
        {
            return true;
        }
        current = current->next;
    }

    return false;
}

// HGETALL - Get all fields and values in hash
char **db_hgetall(Database *db, const char *key, int *count)
{
    *count = 0;
    if (!db || !key)
        return NULL;

    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_HASH)
        return NULL;

    Hash *hash = entry->value.hash_value;
    if (hash->field_count == 0)
        return NULL;

    // Allocate array for field value pairs (2 strings per field)
    char **result = (char **)malloc(hash->field_count * 2 * sizeof(char *));
    if (!result)
        return NULL;

    int idx = 0;
    for (size_t i = 0; i < hash->bucket_count; i++)
    {
        HashField *current = hash->buckets[i];
        while (current)
        {
            result[idx++] = my_strdup(current->field);
            result[idx++] = my_strdup(current->value);

            // Check for allocation failure
            if (!result[idx - 2] || !result[idx - 1])
            {
                // Cleanup on error
                for (int j = 0; j < idx; j++)
                {
                    free(result[j]);
                }
                free(result);
                return NULL;
            }

            current = current->next;
        }
    }

    *count = hash->field_count * 2; // Return total count including both fields and values
    return result;
}

// HDEL - Delete field from hash
bool db_hdel(Database *db, const char *key, const char *field)
{
    if (!db || !key || !field)
        return false;

    Entry *entry = get_entry(db, key);
    if (!entry || entry->type != VALUE_HASH)
        return false;

    Hash *hash = entry->value.hash_value;
    unsigned int field_index = hash_field(field);

    HashField *current = hash->buckets[field_index];
    HashField *prev = NULL;

    while (current)
    {
        if (strcmp(current->field, field) == 0)
        {
            // Remove field
            if (prev)
            {
                prev->next = current->next;
            }
            else
            {
                hash->buckets[field_index] = current->next;
            }

            free(current->field);
            free(current->value);
            free(current);
            hash->field_count--;

            // If hash is empty, remove the key
            if (hash->field_count == 0)
            {
                db_delete(db, key);
            }

            return true;
        }
        prev = current;
        current = current->next;
    }

    return false; // Field not found
}