#ifndef PUBSUB_H
#define PUBSUB_H

#include <stdbool.h>
#include <pthread.h>

// Forward declarations
typedef struct Channel Channel;
typedef struct Subscriber Subscriber;
typedef struct PubSubManager PubSubManager;

// Subscribe structure - represents a client subscribed to channels
typedef struct Subscriber
{
    int client_socket;
    char **channels;         // Array of channel names this client is subscribed to
    size_t channel_count;    // Number of channels subscribed to
    size_t channel_capacity; // Allocated capacity for channels array
    struct Subscriber *next; // For linked list in channel
} Subscriber;

// Channel structure - represents a pub/sub channel
typedef struct Channel
{
    char *name;
    Subscriber *subscribers; // Linked list of subscribers
    size_t subscriber_count;
    struct Channel *next; // For hash table chaining
} Channel;

// Pub/Sub Manager structure
typedef struct PubSubManager
{
    Channel *channels[1024]; // Hash table of channels
    pthread_mutex_t mutex;   // Thread safety
} PubSubManager;

// Function prototypes
PubSubManager *pubsub_create();
void pubsub_free(PubSubManager *pubsub);

// Channel management
Channel *pubsub_get_or_create_channel(PubSubManager *pubsub, const char *channel_name);
void pubsub_remove_empty_channel(PubSubManager *pubsub, const char *channel_name);

// Subscription management
bool pubsub_subscribe(PubSubManager *pubsub, int client_socket, const char *channel_name);
bool pubsub_unsubscribe(PubSubManager *pubsub, int client_socket, const char *channel_name);
void pubsub_unsubscribe_all(PubSubManager *pubsub, int client_socket);

// Message publishing
int pubsub_publish(PubSubManager *pubsub, const char *channel_name, const char *message);

// Utility functions
unsigned int pubsub_hash(const char *str);
bool pubsub_is_subscribed(PubSubManager *pubsub, int client_socket, const char *channel_name);
char **pubsub_get_subscribed_channels(PubSubManager *pubsub, int client_socket, int *count);

#endif /* PUBSUB_H */