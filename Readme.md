Key Value Store

File Organisation

```
mini-redis/
├── include/
│   ├── database.h      # Database structure and core operations
│   ├── commands.h      # KV Store command implementations
│   ├── utils.h         # Utility functions (tokeniser, etc.)
│   └── persistence.h   # Save/load functionality
├── src/
│   ├── database.c      # Implementation of database functions
│   ├── commands.c      # Implementation of KV Store commands
│   ├── utils.c         # Implementation of utility functions
│   ├── persistence.c   # Implementation of save/load functions
│   └── main.c          # Main program entry point
└── Makefile            # Build configuration
```

Design Principles

Separation of Concerns: Each module has a clear responsibility
Encapsulation: The database is encapsulated for easier maintenance
Modularity: Components can be modified independently
Reusability: Core functionality can be reused in other projects

Build Instructions

To build the project:
```
make
```

To clean build artifacts:
```
make clean
```