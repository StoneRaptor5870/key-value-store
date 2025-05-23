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
