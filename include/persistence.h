#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "database.h"
#include <stdbool.h>

// File operations constants
#define DB_FILE_SIGNATURE "KVSTORE"
#define DB_FILE_VERSION 1

// Save and load functions
bool save_command(Database *db, const char *filename);
bool load_command(Database *db, const char *filename);

#endif /* PERSISTENCE_H */