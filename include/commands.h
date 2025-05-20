#ifndef COMMANDS_H
#define COMMANDS_H

#include "database.h"
#include <stdbool.h>

// KV Store implementations
void set_command(Database *db, const char *key, const char *value);
char *get_command(Database *db, const char *key);
bool exists_command(Database *db, const char *key);
bool del_command(Database *db, const char *key);
bool incr_command(Database *db, const char *key, int *new_value);
bool decr_command(Database *db, const char *key, int *new_value);
void print_help();

#endif /* COMMANDS_H */
