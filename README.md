# SO2

## Dining philosophers problem

In the dining philosophers problem a group of N philosophers is trying to eat.
Each of them requires two chopsticks for eating and has one of his chopsticks
on the left and one on the right. However, the philosophers sit side by side
and when they try to it it's possible that one or both or they chopsticks is
taken by their neighbours.

This repository implements a simplified version of K. M. Chandy and J. Misra
algorithm introduced in the paper [The Drinking Philosophers
Problem](https://dl.acm.org/doi/abs/10.1145/1780.1804). In their version of the
solution each philosopher can request a chopstick from neighbouring philosophers.
Their algorithm relies on the fact that requests are analysed in FIFO order.
Which prevents double eating by single philosopher.

To achive this I implemented FIFOMutex class which ensures that when multiple
threads try to acquire a mutex they will be granted access to the resource in
order.


## Multi-threaded Chat Server

A thread-safe TCP chat server implementation in C++ that handles multiple concurrent clients with proper synchronization and resource management.

### Architecture Overview

The server uses a **one-thread-per-client** model where:
- Main thread accepts incoming connections
- Each client gets its own dedicated handler thread
- Shared resources are protected by mutexes and atomic operations

### Thread Safety Strategy

#### Client State Management
- **Atomic socket descriptors**: `std::atomic<int> socket` prevents race conditions during socket operations
- **Atomic active flag**: `std::atomic<bool> active` allows safe state checking across threads
- **Per-client mutexes**: Each client has its own `std::mutex client_mutex` for fine-grained locking

#### Container Safety
- **std::list instead of vector**: Safer for concurrent iteration (no iterator invalidation on insertion)
- **Snapshot pattern**: Broadcast operations create local copies of client lists to avoid holding locks during I/O
- **No runtime removal**: Clients are marked inactive but never removed from containers during operation

#### Key Synchronization Points

##### 1. Client List Access
```cpp
std::mutex clients_mutex; 
```
- Locked when adding new clients
- Locked when creating snapshots for broadcasting
- **Not held during I/O operations** to prevent blocking

##### 2. Individual Client Operations
```cpp
std::mutex client_mutex;  
```
- Protects socket operations (`send`, `recv`)
- Ensures username updates are atomic
- Prevents concurrent access to client state

##### 3. Message History
```cpp
std::mutex history_mutex; 
```
- Synchronizes access to shared message buffer
- Maintains FIFO order for message history

#### Thread Lifecycle Management

##### Client Connection Flow
1. **Accept**: Main thread accepts connection, creates `Client` object
2. **Register**: Client added to container under `clients_mutex`
3. **Spawn**: New thread created for client handling
4. **Detach**: Thread runs independently with shared_ptr keeping client alive

##### Client Disconnection Flow
1. **Mark Inactive**: Set `client->active = false` atomically
2. **Close Socket**: Atomic socket closure prevents further I/O
3. **Natural Cleanup**: Thread exits naturally, shared_ptr cleans up resources

##### Server Shutdown
1. **Signal Stop**: Set global `running = false`
2. **Close Server Socket**: Stops accepting new connections
3. **Mark All Inactive**: Atomically disable all clients
4. **Join Threads**: Wait for all client threads to complete
5. **Clear Containers**: Safe cleanup after all threads finish

### Race Condition Prevention

#### The Iterator Problem
**Problem**: Removing clients during iteration causes segfaults
```cpp

for (auto& client : clients) {
    clients.erase(client);  
}
```

**Solution**: Mark inactive, never remove during operation
```cpp
client->active.store(false);
/
```


### Memory Management

- **RAII**: Client destructor automatically closes sockets
- **shared_ptr**: Automatic cleanup when last reference is dropped
- **No manual delete**: All cleanup handled by smart pointers and RAII

