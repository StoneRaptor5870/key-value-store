#include "../include/persistence.h"
#include "../include/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

// Save command implementation
bool save_command(Database *db, const char *filename)
{
    if (!db || !filename)
        return false;

    char *full_filename;
    size_t len = strlen(filename);

    // Check if filename already ends with .db
    if (len >= 3 && strcasecmp(filename + len - 3, ".db") == 0)
    {
        // Already has .db extension
        full_filename = (char *)filename; // Use as-is
    }
    else
    {
        // Add .db extension
        full_filename = (char *)malloc(len + 4); // +3 for ".db" +1 for null terminator
        if (!full_filename)
        {
            fprintf(stderr, "Failed to allocate memory for filename\n");
            return false;
        }
        strcpy(full_filename, filename);
        strcat(full_filename, ".db");
    }

    FILE *file = fopen(full_filename, "w");

    if (!file)
    {
        fprintf(stderr, "Failed to open file %s for writing: %s\n", full_filename, strerror(errno));

        // Free allocated memory if we created a new filename
        if (full_filename != filename)
            free(full_filename);
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

            // Write entry type
            fprintf(file, "%d\n", current->type);

            // Write expiration
            fprintf(file, "%d\n", (int)current->expiration);

            // Write value length and value
            if (current->type == VALUE_STRING)
            {
                // Write string value
                int value_len = strlen(current->value.string_value);
                fprintf(file, "%d\n", value_len);
                fwrite(current->value.string_value, 1, value_len, file);
                fprintf(file, "\n");
            }
            else if (current->type == VALUE_LIST)
            {
                // Write list length
                List *list = current->value.list_value;
                fprintf(file, "%d\n", (int)list->length);

                // Write each list element
                ListNode *node = list->head;
                while (node)
                {
                    int data_len = strlen(node->data);
                    fprintf(file, "%d\n", data_len);
                    fwrite(node->data, 1, data_len, file);
                    fprintf(file, "\n");
                    node = node->next;
                }
            }
            current = current->next;
        }
    }

    fclose(file);

    // Free allocated memory if we created a new filename
    if (full_filename != filename)
        free(full_filename);

    return true;
}

// LOAD command implementation
bool load_command(Database *db, const char *filename)
{
    if (!db || !filename)
        return false;

    char *full_filename;
    size_t len = strlen(filename);

    // Check if filename already ends with .db
    if (len >= 3 && strcasecmp(filename + len - 3, ".db") == 0)
    {
        // Already has .db extension
        full_filename = (char *)filename; // Use as-is
    }
    else
    {
        // Add .db extension
        full_filename = (char *)malloc(len + 4); // +3 for ".db" +1 for null terminator
        if (!full_filename)
        {
            fprintf(stderr, "Failed to allocate memory for filename\n");
            return false;
        }
        strcpy(full_filename, filename);
        strcat(full_filename, ".db");
    }

    FILE *file = fopen(full_filename, "r");
    if (!file)
    {
        fprintf(stderr, "Failed to open file %s for reading: %s\n", full_filename, strerror(errno));

        // Free allocated memory if we created a new filename
        if (full_filename != filename)
            free(full_filename);
        return false;
    }

    // Read and verify file signature
    char signature[32];
    if (fscanf(file, "%31s\n", signature) != 1 || strcmp(signature, DB_FILE_SIGNATURE) != 0)
    {
        fprintf(stderr, "Invalid database file format: wrong signature\n");
        fclose(file);

        // Free allocated memory if we created a new filename
        if (full_filename != filename)
            free(full_filename);
        return false;
    }

    // Read and verify version
    int version;
    if (fscanf(file, "%d\n", &version) != 1 || version != DB_FILE_VERSION)
    {
        fprintf(stderr, "Unsupported database file version: %d\n", version);
        fclose(file);

        // Free allocated memory if we created a new filename
        if (full_filename != filename)
            free(full_filename);
        return false;
    }

    // Read entry count
    int entry_count;
    if (fscanf(file, "%d\n", &entry_count) != 1)
    {
        fprintf(stderr, "Failed to read entry count\n");
        fclose(file);

        // Free allocated memory if we created a new filename
        if (full_filename != filename)
            free(full_filename);
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

            if (current->type == VALUE_STRING)
            {
                free(current->value.string_value);
            }
            else if (current->type == VALUE_LIST)
            {
                free(current->value.list_value);
            }
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

            // Free allocated memory if we created a new filename
            if (full_filename != filename)
                free(full_filename);
            return false;
        }

        // Read key
        char *key = (char *)malloc(key_len + 1);
        if (!key)
        {
            fprintf(stderr, "Failed to allocate memory for key\n");
            fclose(file);

            // Free allocated memory if we created a new filename
            if (full_filename != filename)
                free(full_filename);
            return false;
        }

        if (fread(key, 1, key_len, file) != (size_t)key_len)
        {
            fprintf(stderr, "Failed to read key for entry %d\n", i);
            free(key);
            fclose(file);

            // Free allocated memory if we created a new filename
            if (full_filename != filename)
                free(full_filename);
            return false;
        }
        key[key_len] = '\0';

        // Skip newline
        fgetc(file);

        // Read entry type
        int type;
        if (fscanf(file, "%d\n", &type) != 1)
        {
            fprintf(stderr, "Failed to read type for entry %d\n", i);
            free(key);
            fclose(file);

            // Free allocated memory if we created a new filename
            if (full_filename != filename)
                free(full_filename);
            return false;
        }

        // Read expiration
        int expiration;
        if (fscanf(file, "%d\n", &expiration) != 1)
        {
            fprintf(stderr, "Failed to read expiration for entry %d\n", i);
            free(key);
            fclose(file);

            // Free allocated memory if we created a new filename
            if (full_filename != filename)
                free(full_filename);
            return false;
        }

        if (type == VALUE_STRING)
        {
            // Read string value
            int value_len;
            if (fscanf(file, "%d\n", &value_len) != 1)
            {
                fprintf(stderr, "Failed to read value length for entry %d\n", i);
                free(key);
                fclose(file);

                // Free allocated memory if we created a new filename
                if (full_filename != filename)
                    free(full_filename);
                return false;
            }

            char *value = (char *)malloc(value_len + 1);
            if (!value)
            {
                fprintf(stderr, "Failed to allocate memory for value\n");
                free(key);
                fclose(file);

                // Free allocated memory if we created a new filename
                if (full_filename != filename)
                    free(full_filename);
                return false;
            }

            if (fread(value, 1, value_len, file) != (size_t)value_len)
            {
                fprintf(stderr, "Failed to read value for entry %d\n", i);
                free(key);
                free(value);
                fclose(file);

                // Free allocated memory if we created a new filename
                if (full_filename != filename)
                    free(full_filename);
                return false;
            }
            value[value_len] = '\0';
            fgetc(file); // Skip newline

            // Store string value
            db_set(db, key, value);
            db_set_expiration(db, key, (time_t)expiration);

            free(value);
        }
        else if (type == VALUE_LIST)
        {
            // Read list length
            int list_len;
            if (fscanf(file, "%d\n", &list_len) != 1)
            {
                fprintf(stderr, "Failed to read list length for entry %d\n", i);
                free(key);
                fclose(file);

                // Free allocated memory if we created a new filename
                if (full_filename != filename)
                    free(full_filename);
                return false;
            }

            // Read list elements and reconstruct using rpush
            for (int j = 0; j < list_len; j++)
            {
                int data_len;
                if (fscanf(file, "%d\n", &data_len) != 1)
                {
                    fprintf(stderr, "Failed to read list element length for entry %d, element %d\n", i, j);
                    free(key);
                    fclose(file);

                    // Free allocated memory if we created a new filename
                    if (full_filename != filename)
                        free(full_filename);
                    return false;
                }

                char *data = (char *)malloc(data_len + 1);
                if (!data)
                {
                    fprintf(stderr, "Failed to allocate memory for list element\n");
                    free(key);
                    fclose(file);

                    // Free allocated memory if we created a new filename
                    if (full_filename != filename)
                        free(full_filename);
                    return false;
                }

                if (fread(data, 1, data_len, file) != (size_t)data_len)
                {
                    fprintf(stderr, "Failed to read list element for entry %d, element %d\n", i, j);
                    free(key);
                    free(data);
                    fclose(file);

                    // Free allocated memory if we created a new filename
                    if (full_filename != filename)
                        free(full_filename);
                    return false;
                }
                data[data_len] = '\0';
                fgetc(file); // Skip newline

                // Add to list (this will create the list entry on first call)
                if (!db_rpush(db, key, data))
                {
                    fprintf(stderr, "Failed to add element to list for key %s\n", key);
                    // free(key);
                    free(data);
                    fclose(file);

                    // Free allocated memory if we created a new filename
                    if (full_filename != filename)
                        free(full_filename);
                    return false;
                }

                free(data);
            }

            // Set expiration for the list
            if (expiration != 0)
            {
                db_set_expiration(db, key, (time_t)expiration);
            }
        }

        free(key);
    }

    fclose(file);

    // Free allocated memory if we created a new filename
    if (full_filename != filename)
        free(full_filename);
    return true;
}