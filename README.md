# PointerPro Animate

PointerPro Animate is a graphics and animation project consisting of two major components:

* **libanimate**: a graphics and animation library developed in C which allows the management of canvases, sprites and placements, as well as the rendering of animation frames.
* **animate-platform**: a client and server system that enables collaborative managment of objects for multiple users simultaneously through remote procedure calls (RPCs) to the `libanimate` library.

This repository combines both components into a single project, with the networking layer building on top of the animation functionality provided by the library.

### libanimate

The `libanimate` directory contains the animation library implementation.

Features include:

- Canvas creation and destruction
- Sprite loading from bitmap file
- Rectangle and circle sprite generation
- Sprite placement and layer management
- Animation parameter configuration
- Frame generation

The library exposes an abstract API for constructing animated scenes and generating rendered frame data.

### animate-platform

The `animate-platform` directory contains a distributed client and server system built around animation canvases.

The server:

- Accepts multiple concurrent client connections
- Authenticates users
- Processes animation RPC requests
- Supports shared canvases between users
- Provides synchronisation using barriers
- Generates animation output files

The client:

- Connects to the server using POSIX signals and FIFOs
- Sends user commands to the server
- Displays responses returned by the server
- Supports collaborative canvas workflows

## Building

### Animation Library

```bash
cd libanimate
make # Makefile contains build instructions
```

### Client-Server System

```bash
cd server-client
make
```

## Running the Server

Start the server and specify the worker thread pool size:

```bash
./animate_server <threadpool_size>
```

Example:

```bash
./animate_server 4
```

The server will display its process identifier:

```text
Server PID: 12345
```

## Running a Client

Use the server PID when launching a client:

```bash
./animate_client <server_pid>
```

Example:

```bash
./animate_client 12345
```

Multiple clients may connect to the same server simultaneously.

## Authentication

Users authenticate using credentials stored in `users.txt`.

Example:

```text
Login Alice
```

Successful login:

```text
Welcome Alice. Your balance is 10
```

Failed login:

```text
Reject UNAUTHORISED
```

or

```text
Reject BALANCE
```

## Collaborative Features

The client-server system supports:

- Shared canvases between users
- Concurrent canvas operations
- Barrier synchronisation
- Ordered RPC responses
- Animation generation using ffmpeg

Users may share canvas identifiers and collaboratively modify the same animation project while maintaining per-user operation ordering.

## Testing

A combination of through manual and automated testing has been conducted to test the following aspects of the project:

- Animation library functionality
- Client and server communication
- Authentication
- Shared canvas behaviour
- Synchronisation barriers
- Concurrent request processing
- Resource cleanup and disconnection handling
- Ensuring no memory leaks
- Prevention of live locks in system

