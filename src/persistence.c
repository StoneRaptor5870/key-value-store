#include "../include/persistence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Save command implementation
bool save_command(Database *db, const char *filename)
{
    FILE *file = fopen(filename, "wb");

    if (!file)
    {
        fprintf(stderr, "Failed to open file %s for writing: %s\n", filename, strerror(errno));
        return false;
    }

    // Write file signature and version
    fprintf(file, "%s\n%d\n", DB_FILE_SIGNATURE, DB_FILE_VERSION);

    // Write entries
    int total_entries = 0;

    // First pass to count entries
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        Entry *current = db->hash_table[i];
        while (current)
        {
            total_entries++;
            current = current->next;
        }
    }

    // Write entry count
    fprintf(file, "%d\n", total_entries);

    // Write each entry
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        Entry *current = db->hash_table[i];
        while (current)
        {
            // Write key length and key
            int key_len = strlen(current->key);
            fprintf(file, "%d\n", key_len);
            fwrite(current->key, 1, key_len, file);
            fprintf(file, "\n");

            // Write value length and value
            int value_len = strlen(current->value);
            fprintf(file, "%d\n", value_len);
            fwrite(current->value, 1, value_len, file);
            fprintf(file, "\n");

            current = current->next;
        }
    }

    fclose(file);
    return true;
}

// LOAD command implementation
bool load_command(Database *db, const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        fprintf(stderr, "Failed to open file %s for reading: %s\n", filename, strerror(errno));
        return false;
    }

    // Read and verify file signature
    char signature[32];
    if (fscanf(file, "%31s\n", signature) != 1 || strcmp(signature, DB_FILE_SIGNATURE) != 0)
    {
        fprintf(stderr, "Invalid database file format: wrong signature\n");
        fclose(file);
        return false;
    }

    // Read and verify version
    int version;
    if (fscanf(file, "%d\n", &version) != 1 || version != DB_FILE_VERSION)
    {
        fprintf(stderr, "Unsupported database file version: %d\n", version);
        fclose(file);
        return false;
    }

    // Read entry count
    int entry_count;
    if (fscanf(file, "%d\n", &entry_count) != 1)
    {
        fprintf(stderr, "Failed to read entry count\n");
        fclose(file);
        return false;
    }

    // Clear existing database
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
        db->hash_table[i] = NULL;
    }

    // Read entries
    for (int i = 0; i < entry_count; i++)
    {
        // Read key length
        int key_len;
        if (fscanf(file, "%d\n", &key_len) != 1)
        {
            fprintf(stderr, "Failed to read key length for entry %d\n", i);
            fclose(file);
            return false;
        }

        // Read key
        char *key = (char *)malloc(key_len + 1);
        if (!key)
        {
            fprintf(stderr, "Failed to allocate memory for key\n");
            fclose(file);
            return false;
        }

        if (fread(key, 1, key_len, file) != (size_t)key_len)
        {
            fprintf(stderr, "Failed to read key for entry %d\n", i);
            free(key);
            fclose(file);
            return false;
        }
        key[key_len] = '\0';

        // Skip newline
        fgetc(file);

        // Read value length
        int value_len;
        if (fscanf(file, "%d\n", &value_len) != 1)
        {
            fprintf(stderr, "Failed to read value length for entry %d\n", i);
            free(key);
            fclose(file);
            return false;
        }

        // Read value
        char *value = (char *)malloc(value_len + 1);
        if (!value)
        {
            fprintf(stderr, "Failed to allocate memory for value\n");
            free(key);
            fclose(file);
            return false;
        }

        if (fread(value, 1, value_len, file) != (size_t)value_len)
        {
            fprintf(stderr, "Failed to read value for entry %d\n", i);
            free(key);
            free(value);
            fclose(file);
            return false;
        }
        value[value_len] = '\0';

        // Skip newline
        fgetc(file);

        // Store in database
        db_set(db, key, value);

        free(key);
        free(value);
    }

    fclose(file);
    return true;
}