#ifndef COMMANDS_H
#define COMMANDS_H

#include "database.h"
#include <stdbool.h>
#include <time.h>

// KV Store string implementations
void set_command(Database *db, const char *key, const char *value);
char *get_command(Database *db, const char *key);
bool exists_command(Database *db, const char *key);
bool del_command(Database *db, const char *key);
bool incr_command(Database *db, const char *key, int *new_value);
bool decr_command(Database *db, const char *key, int *new_value);

// TTL command implementations
bool expire_command(Database *db, const char *key, int seconds);
int ttl_command(Database *db, const char *key);
bool persist_command(Database *db, const char *key);

// List commands
bool lpush_command(Database *db, const char *key, const char *value);
bool rpush_command(Database *db, const char *key, const char *value);
char *lpop_command(Database *db, const char *key);
char *rpop_command(Database *db, const char *key);
char **lrange_command(Database *db, const char *key, int start, int stop, int *count);
int llen_command(Database *db, const char *key);

// Utility function
void print_help();

#endif /* COMMANDS_H */
