# Key-Value Store with TCP Server

A simple key-value store with both CLI and TCP server functionality.

## Features

- In-memory key-value database
- Command Line Interface (CLI) for direct interaction
- TCP server for remote connections
- Redis-compatible protocol
- Basic key operations: SET, GET, DEL, EXISTS
- Integer operations: INCR, DECR
- Key expiry & TTL operations: EXPIRE, TTL, PERSIST
- List operations: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE
- Hash operations: HSET, HGET, HGETALL, HEXISTS, HDEL
- PUB/SUB Commands: SUBSCRIBE, PUBLISH, UNSUBSCRIBE
- Persistence: SAVE, LOAD
- Compatible with Redis clients

## Building

```bash
make
```

The executable will be built in the `bin` directory.

## Usage

### Server Mode (Default)

```bash
bin/kv-store
```

This will start the server on the default port (8520).

You can specify a different port:

```bash
bin/kv-store -p 7000
```

### Interactive Mode (CLI)

```bash
bin/kv-store -i
```

### Loading a Database at Startup

```bash
bin/kv-store -f filename
```

### Command Line Help

```bash
bin/kv-store -h
```

## Available Commands

### Basic Commands

- `SET key value` - Set key to hold string value
- `GET key` - Get the value of key
- `DEL key` - Delete key
- `EXISTS key` - Check if key exists
- `INCR key` - Increment the integer value of key by one
- `DECR key` - Decrement the integer value of key by one

### Key Expiry & TTL Commands

- `EXPIRE key time` - Set a key to expire in N seconds
- `TTL key` - Get remaining time to live
- `PERSIST key` - Remove expiration from a key

### List Commands

- `LPUSH key value` - Insert value at the head (left) of the list
- `RPUSH key value` - Insert value at the tail (right) of the list
- `LPOP key` - Remove and return the first element (left) of the list
- `RPOP key` - Remove and return the last element (right) of the list
- `LLEN key` - Get the length of the list
- `LRANGE key start stop` - Get a range of elements from the list

### Hash Commands

- `HSET key field value` – Sets one field-value pairs in a hash
- `HGET key field` – Retrieves the value of a specific field in a hash
- `HGETALL key` – Returns all fields and values in a hash
- `HEXISTS key field` – Checks if a field exists in a hash
- `HDEL key field` – Deletes one fields from a hash

### Pub/Sub Commands

- `PUBLISH channel message` – Sends a message to all clients subscribed to the given channel
- `SUBSCRIBE channel` – Subscribes the client to one or more channels
- `UNSUBSCRIBE channel` – Unsubscribes the client from one or more channels (or all if none given)

### Persistence Commands

- `SAVE filename` - Save the database to a file
- `LOAD filename` - Load the database from a file

### Server Commands

- `INFO` - Get server information
- `PING` - Test connection (returns PONG)
- `QUIT` or `EXIT` - Close the connection

## Connecting with Redis Clients

You can connect to the server using any Redis client. For example, using the redis-cli:

```bash
redis-cli -p 8520
```

Then you can use all the supported commands:

```
127.0.0.1:8520> SET mykey "Hello, World!"
OK
127.0.0.1:8520> GET mykey
"Hello, World!"
127.0.0.1:8520> EXISTS mykey
(integer) 1
127.0.0.1:8520> DEL mykey
(integer) 1
127.0.0.1:8520> PING
PONG
```
