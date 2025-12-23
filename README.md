# High-Performance Systems in C

This repository contains implementations of high-performance systems programming projects in C, based on the CS:APP (Computer Systems: A Programmer's Perspective) curriculum. Each project focuses on different aspects of systems programming, including memory management, caching, networking, and process control.

## Projects

### 1. Cache Lab (`cache/`)
Implementation of a cache simulator that models the behavior of CPU caches. This lab focuses on understanding cache organization, replacement policies, and the impact of cache performance on program execution.

**Key Components:**
- Cache simulator (`csim.c`)
- Matrix transpose optimization (`trans.c`)
- Trace-driven testing framework

### 2. Malloc Lab (`malloc/`)
Custom implementation of a dynamic memory allocator using explicit free lists. This lab explores memory management, heap organization, and performance optimization techniques.

**Key Components:**
- Memory allocator (`mm.c`)
- Performance benchmarking tools
- Trace-driven testing with various workloads

### 3. Proxy Lab (`proxy/`)
A concurrent web proxy server that can handle multiple client requests simultaneously. This lab covers network programming, concurrency, and HTTP protocol implementation.

**Key Components:**
- Proxy server (`proxy.c`)
- HTTP request/response handling
- LRU cache implementation
- Concurrent request handling

### 4. Shell Lab (`shell/`)
Implementation of a Unix shell (`tsh`) with job control, signal handling, and process management capabilities. This lab demonstrates understanding of process control, signals, and I/O redirection.

**Key Components:**
- Shell implementation (`tsh.c`)
- Job control and signal handling
- Built-in commands support
- Background/foreground process management

## Building and Running

Each project has its own `Makefile`. Navigate to the respective directory and use:

```bash
make          # Build the project
make clean    # Clean build artifacts
```

For specific build instructions, refer to the README files in each project directory.

## Requirements

- GCC compiler
- Make
- Standard C library
- Linux/Unix environment (for Shell Lab)

## Repository Structure

```
High-Performance-Systems-in-C/
├── cache/          # Cache simulator implementation
├── malloc/         # Memory allocator implementation
├── proxy/          # Web proxy server
├── shell/          # Unix shell implementation
└── README.md       # This file
```

## License

This repository contains educational projects based on the CS:APP curriculum.

