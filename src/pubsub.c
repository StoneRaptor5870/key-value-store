#include "../include/pubsub.h"
#include "../include/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

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

// Hash function for channel names
unsigned int pubsub_hash(const char *str)
{
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % 1024;
}

// Create pub/sub manager
PubSubManager *pubsub_create()
{
    PubSubManager *pubsub = malloc(sizeof(PubSubManager));
    if (!pubsub)
        return NULL;

    // Initialise hash table
    for (int i = 0; i < 1024; i++)
    {
        pubsub->channels[i] = NULL;
    }

    // Initialise mutex
    if (pthread_mutex_init(&pubsub->mutex, NULL) != 0)
    {
        free(pubsub);
        return NULL;
    }

    return pubsub;
}

// Free subscriber
static void free_subscriber(Subscriber *sub)
{
    if (!sub)
        return;

    // Free channel names array
    if (sub->channels)
    {
        for (size_t i = 0; i < sub->channel_count; i++)
        {
            free(sub->channels[i]);
        }
        free(sub->channels);
    }

    free(sub);
}

// Free channel
static void free_channel(Channel *channel)
{
    if (!channel)
        return;

    free(channel->name);

    // Free all subscriber
    Subscriber *sub = channel->subscribers;
    while (sub)
    {
        Subscriber *next = sub->next;
        free_subscriber(sub);
        sub = next;
    }

    free(channel);
}

// Free Pub/Sub manager
void pubsub_free(PubSubManager *pubsub)
{
    if (!pubsub)
        return;

    pthread_mutex_lock(&pubsub->mutex);

    // Free all channels
    for (int i = 0; i < 1024; i++)
    {
        Channel *channel = pubsub->channels[i];
        while (channel)
        {
            Channel *next = channel->next;
            free_channel(channel);
            channel = next;
        }
    }

    pthread_mutex_unlock(&pubsub->mutex);
    pthread_mutex_destroy(&pubsub->mutex);
    free(pubsub);
}

// Get or create channel
Channel *pubsub_get_or_create_channel(PubSubManager *pubsub, const char *channel_name)
{
    if (!pubsub || !channel_name)
        return NULL;

    unsigned int index = pubsub_hash(channel_name);

    // Look for existing channel
    Channel *channel = pubsub->channels[index];
    while (channel)
    {
        if (strcmp(channel->name, channel_name) == 0)
        {
            return channel;
        }
        channel = channel->next;
    }

    // Create new channel
    channel = malloc(sizeof(Channel));
    if (!channel)
        return NULL;

    channel->name = my_strdup(channel_name);
    if (!channel->name)
    {
        free(channel);
        return NULL;
    }

    channel->subscribers = NULL;
    channel->subscriber_count = 0;
    channel->next = pubsub->channels[index];
    pubsub->channels[index] = channel;

    return channel;
}

// Remove empty channel
void pubsub_remove_empty_channel(PubSubManager *pubsub, const char *channel_name)
{
    if (!pubsub || !channel_name)
        return;

    unsigned int index = pubsub_hash(channel_name);
    Channel **current = &pubsub->channels[index];

    while (*current)
    {
        if (strcmp((*current)->name, channel_name) == 0)
        {
            if ((*current)->subscriber_count == 0)
            {
                Channel *to_remove = *current;
                *current = (*current)->next;
                free_channel(to_remove);
                return;
            }
            break;
        }
        current = &(*current)->next;
    }
}

// Find subscriber in channel
static Subscriber *find_subscriber_in_channel(Channel *channel, int client_socket)
{
    Subscriber *sub = channel->subscribers;
    while (sub)
    {
        if (sub->client_socket == client_socket)
        {
            return sub;
        }
        sub = sub->next;
    }

    return NULL;
}

// Add channel to subscriber's list
static bool add_channel_to_subscriber(Subscriber *sub, const char *channel_name)
{
    // Check if already in list
    for (size_t i = 0; i < sub->channel_count; i++)
    {
        if (strcmp(sub->channels[i], channel_name) == 0)
        {
            return true; // Already exists
        }
    }

    // Expand capacity if needed
    if (sub->channel_count >= sub->channel_capacity)
    {
        size_t new_capacity = sub->channel_capacity == 0 ? 4 : sub->channel_capacity * 2;
        char **new_channels = realloc(sub->channels, new_capacity * sizeof(char *));
        if (!new_channels)
            return false;

        sub->channels = new_channels;
        sub->channel_capacity = new_capacity;
    }

    // Add channel
    sub->channels[sub->channel_count] = my_strdup(channel_name);
    if (!sub->channels[sub->channel_count])
        return false;

    sub->channel_count++;
    return true;
}

// Remove channel from subscriber's list
static void remove_channel_from_subscriber(Subscriber *sub, const char *channel_name)
{
    for (size_t i = 0; i < sub->channel_count; i++)
    {
        if (strcmp(sub->channels[i], channel_name) == 0)
        {
            free(sub->channels[i]);
            // Shift remaining channels
            for (size_t j = i; j < sub->channel_count - 1; j++)
            {
                sub->channels[j] = sub->channels[j + 1];
            }
            sub->channel_count--;
            return;
        }
    }
}

// Subscribe to channel
bool pubsub_subscribe(PubSubManager *pubsub, int client_socket, const char *channel_name)
{
    if (!pubsub || !channel_name || client_socket < 0)
        return false;

    pthread_mutex_lock(&pubsub->mutex);

    Channel *channel = pubsub_get_or_create_channel(pubsub, channel_name);
    if (!channel)
    {
        pthread_mutex_unlock(&pubsub->mutex);
        return false;
    }

    // Check if already subscribed
    Subscriber *existing = find_subscriber_in_channel(channel, client_socket);
    if (existing)
    {
        pthread_mutex_unlock(&pubsub->mutex);
        return true; // Already subscribed
    }

    // Create new subscriber or find existing one
    Subscriber *sub = NULL;

    // Look for existing subscriber in other channels (to reuse the subscriber object)
    for (int i = 0; i < 1024 && !sub; i++)
    {
        Channel *other_channel = pubsub->channels[i];
        while (other_channel && !sub)
        {
            sub = find_subscriber_in_channel(other_channel, client_socket);
            other_channel = other_channel->next;
        }
    }

    if (!sub)
    {
        // Create new subscriber
        sub = malloc(sizeof(Subscriber));
        if (!sub)
        {
            pthread_mutex_unlock(&pubsub->mutex);
            return false;
        }

        sub->client_socket = client_socket;
        sub->channels = NULL;
        sub->channel_count = 0;
        sub->channel_capacity = 0;
    }

    // Add channel to subscriber's list
    if (!add_channel_to_subscriber(sub, channel_name))
    {
        if (sub->channel_count == 0)
        {
            free_subscriber(sub);
        }
        pthread_mutex_unlock(&pubsub->mutex);
        return false;
    }

    // Add subscriber to channel (only if not already there)
    if (!find_subscriber_in_channel(channel, client_socket))
    {
        sub->next = channel->subscribers;
        channel->subscribers = sub;
        channel->subscriber_count++;
    }

    pthread_mutex_unlock(&pubsub->mutex);
    return true;
}

// Unsubscribe from channel
bool pubsub_unsubscribe(PubSubManager *pubsub, int client_socket, const char *channel_name)
{
    if (!pubsub || !channel_name || client_socket < 0)
        return false;

    pthread_mutex_lock(&pubsub->mutex);

    unsigned int index = pubsub_hash(channel_name);
    Channel *channel = pubsub->channels[index];

    while (channel)
    {
        if (strcmp(channel->name, channel_name) == 0)
        {
            // Remove subscriber from channel
            Subscriber **current = &channel->subscribers;
            while (*current)
            {
                if ((*current)->client_socket == client_socket)
                {
                    Subscriber *to_remove = *current;
                    *current = (*current)->next;
                    channel->subscriber_count--;

                    // Remove channel from subscriber's list
                    remove_channel_from_subscriber(to_remove, channel_name);

                    // If subscriber has no more channels, free it
                    if (to_remove->channel_count == 0)
                    {
                        free_subscriber(to_remove);
                    }

                    // Remove empty channel
                    if (channel->subscriber_count == 0)
                    {
                        pubsub_remove_empty_channel(pubsub, channel_name);
                    }

                    pthread_mutex_unlock(&pubsub->mutex);
                    return true;
                }
                current = &(*current)->next;
            }
            break;
        }
        channel = channel->next;
    }
    pthread_mutex_unlock(&pubsub->mutex);
    return false;
}

// Unsubscribe from all channels
void pubsub_unsubscribe_all(PubSubManager *pubsub, int client_socket)
{
    if (!pubsub || client_socket < 0)
        return;

    pthread_mutex_lock(&pubsub->mutex);

    // Find all channels this client is subscribed to
    char **channels_to_unsubscribe = NULL;
    int channel_count = 0;

    for (int i = 0; i < 1024; i++)
    {
        Channel *channel = pubsub->channels[i];
        while (channel)
        {
            if (find_subscriber_in_channel(channel, client_socket))
            {
                // Add to list of channels to unsubscribe from
                channels_to_unsubscribe = realloc(channels_to_unsubscribe,
                                                  (channel_count + 1) * sizeof(char *));
                if (channels_to_unsubscribe)
                {
                    channels_to_unsubscribe[channel_count] = my_strdup(channel->name);
                    channel_count++;
                }
            }
            channel = channel->next;
        }
    }

    pthread_mutex_unlock(&pubsub->mutex);

    // Unsubscribe from each channel
    for (int i = 0; i < channel_count; i++)
    {
        if (channels_to_unsubscribe[i])
        {
            pubsub_unsubscribe(pubsub, client_socket, channels_to_unsubscribe[i]);
            free(channels_to_unsubscribe[i]);
        }
    }

    free(channels_to_unsubscribe);
}

// Publish message to channel
int pubsub_publish(PubSubManager *pubsub, const char *channel_name, const char *message)
{
    if (!pubsub || !channel_name || !message)
        return 0;

    pthread_mutex_lock(&pubsub->mutex);

    unsigned int index = pubsub_hash(channel_name);
    Channel *channel = pubsub->channels[index];

    while (channel)
    {
        if (strcmp(channel->name, channel_name) == 0)
        {
            int delivered = 0;

            // Send message to all subscribers
            Subscriber *sub = channel->subscribers;
            while (sub)
            {
                // Format message as Redis pub/sub message
                char response[4096];
                snprintf(response, sizeof(response),
                         "*3\r\n$7\r\nmessage\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n",
                         strlen(channel_name), channel_name,
                         strlen(message), message);

                // Send to subscriber (non-blocking)
                ssize_t result = send(sub->client_socket, response, strlen(response), MSG_NOSIGNAL);
                if (result > 0)
                {
                    delivered++;
                }
                else
                {
                    // Client might be disconnected - we'll handle cleanup elsewhere
                    printf("Failed to deliver message to client %d\n", sub->client_socket);
                }

                sub = sub->next;
            }
            pthread_mutex_unlock(&pubsub->mutex);
            return delivered;
        }
        channel = channel->next;
    }

    pthread_mutex_unlock(&pubsub->mutex);
    return 0; // Channel not found
}

// Check if client is subscribed to channel
bool pubsub_is_subscribed(PubSubManager *pubsub, int client_socket, const char *channel_name)
{
    if (!pubsub || !channel_name || client_socket < 0)
        return false;

    pthread_mutex_lock(&pubsub->mutex);

    unsigned int index = pubsub_hash(channel_name);
    Channel *channel = pubsub->channels[index];

    while (channel)
    {
        if (strcmp(channel->name, channel_name) == 0)
        {
            bool found = find_subscriber_in_channel(channel, client_socket) != NULL;
            pthread_mutex_unlock(&pubsub->mutex);
            return found;
        }
        channel = channel->next;
    }

    pthread_mutex_unlock(&pubsub->mutex);
    return false;
}

// Get list of channels client is subscribed to
char **pubsub_get_subscribed_channels(PubSubManager *pubsub, int client_socket, int *count)
{
    if (!pubsub || client_socket < 0 || !count)
    {
        if (count)
            *count = 0;
        return NULL;
    }

    pthread_mutex_lock(&pubsub->mutex);

    *count = 0;
    char **result = NULL;

    for (int i = 0; i < 1024; i++)
    {
        Channel *channel = pubsub->channels[i];
        while (channel)
        {
            if (find_subscriber_in_channel(channel, client_socket))
            {
                result = realloc(result, (*count + 1) * sizeof(char *));
                if (result)
                {
                    result[*count] = my_strdup(channel->name);
                    (*count)++;
                }
            }
            channel = channel->next;
        }
    }

    pthread_mutex_unlock(&pubsub->mutex);
    return result;
}