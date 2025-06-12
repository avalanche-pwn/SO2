#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <list>

class ChatServer {
private:
  void setSocketTimeouts(int socket) {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  }

  ssize_t receiveWithTimeout(int socket, char *buffer, size_t size, int timeout_sec = 5) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    int select_result = select(socket + 1, &read_fds, nullptr, nullptr, &timeout);

    if (select_result == -1) {
      return -1;
    }

    if (select_result == 0) {
      return 0;
    }

    return recv(socket, buffer, size, 0);
  }

  struct Client {
    std::atomic<int> socket;
    std::string username;
    std::atomic<bool> active{true};
    std::mutex client_mutex; 
    
    explicit Client(int sock) : socket(sock) {}
    
    ~Client() {
      int sock = socket.load();
      if (sock != -1) {
        close(sock);
      }
    }
    
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;
  };

  int server_socket;
  std::list<std::shared_ptr<Client>> clients;
  std::mutex clients_mutex;
  std::atomic<bool> running{true};
  const int PORT = 8080;

  std::mutex history_mutex;
  std::vector<std::pair<std::string, std::string>> history;

  std::vector<std::thread> client_threads;
  std::mutex threads_mutex;

public:
  ChatServer() : server_socket(-1) {}

  ~ChatServer() { 
    shutdown(); 
  }

  bool start() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
      std::cerr << "Failed to create socket\n";
      return false;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
      std::cerr << "Failed to bind socket\n";
      close(server_socket);
      return false;
    }

    if (listen(server_socket, 10) == -1) {
      std::cerr << "Failed to listen on socket\n";
      close(server_socket);
      return false;
    }

    std::cout << "Chat server started on port " << PORT << std::endl;
    std::cout << "Clients can connect using: netcat localhost " << PORT << std::endl;
    std::cout << "Press Ctrl+C to stop the server\n\n";

    return true;
  }

  void run() {
    while (running.load()) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(server_socket, &read_fds);

      struct timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      int select_result = select(server_socket + 1, &read_fds, nullptr, nullptr, &timeout);

      if (select_result == -1) {
        if (running.load() && errno != EINTR) {
          std::cerr << "Select error: " << strerror(errno) << std::endl;
        }
        continue;
      }

      if (select_result == 0) {
        continue;
      }

      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);

      int client_socket = accept(server_socket, (sockaddr *)&client_addr, &client_len);
      if (client_socket == -1) {
        if (running.load()) {
          std::cerr << "Failed to accept client connection\n";
        }
        continue;
      }

      setSocketTimeouts(client_socket);

      auto client = std::make_shared<Client>(client_socket);
      
      {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(client);
      }

      {
        std::lock_guard<std::mutex> lock(threads_mutex);
        client_threads.emplace_back([this, client]() { 
          handleClient(client); 
        });
      }

      std::cout << "New client connected from " << inet_ntoa(client_addr.sin_addr) << std::endl;
    }
  }

  void shutdown() {
    running.store(false);
    std::cout << "\nInitiating server shutdown...\n";

    if (server_socket != -1) {
      close(server_socket);
      server_socket = -1;
    }

    {
      std::lock_guard<std::mutex> lock(clients_mutex);
      for (auto& client : clients) {
        client->active.store(false);
        int sock = client->socket.load();
        if (sock != -1) {
          close(sock);
          client->socket.store(-1);
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    {
      std::lock_guard<std::mutex> lock(threads_mutex);
      for (auto& thread : client_threads) {
        if (thread.joinable()) {
          thread.join();
        }
      }
      client_threads.clear();
    }

    {
      std::lock_guard<std::mutex> lock(clients_mutex);
      clients.clear();
    }

    std::cout << "Server shutdown complete.\n";
  }

private:
  void handleClient(std::shared_ptr<Client> client) {
    if (!getUsernameFromClient(client)) {
      disconnectClient(client);
      return;
    }

    sendWelcomeMessages(client);
    sendChatHistory(client);
    
    broadcastMessage(client->username + " has joined the chat!", client);

    char buffer[1024];
    while (running.load() && client->active.load()) {
      int sock = client->socket.load();
      if (sock == -1) break;

      ssize_t bytes_received = receiveWithTimeout(sock, buffer, sizeof(buffer) - 1, 1);

      if (bytes_received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        break;
      }

      if (bytes_received == 0) {
        continue;
      }

      buffer[bytes_received] = '\0';
      std::string message(buffer);

      message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
      message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());

      if (message.empty()) continue;

      if (message[0] == '/') {
        handleCommand(client, message);
      } else {
        addToHistory(client->username, message);
        broadcastMessage("[" + client->username + "]: " + message, client);
      }
    }

    if (running.load() && !client->username.empty()) {
      broadcastMessage(client->username + " has left the chat.", client);
    }

    disconnectClient(client);
  }

  void disconnectClient(std::shared_ptr<Client> client) {
    std::lock_guard<std::mutex> client_lock(client->client_mutex);
    
    client->active.store(false);
    int sock = client->socket.load();
    if (sock != -1) {
      close(sock);
      client->socket.store(-1);
    }
  }

  bool getUsernameFromClient(std::shared_ptr<Client> client) {
    if (!safeClientSend(client, "Enter your username: ")) {
      return false;
    }

    char buffer[256];
    int sock = client->socket.load();
    if (sock == -1) return false;

    ssize_t bytes_received = receiveWithTimeout(sock, buffer, sizeof(buffer) - 1, 10);

    if (bytes_received <= 0) {
      return false;
    }

    buffer[bytes_received] = '\0';
    std::string username(buffer);

    username.erase(std::remove(username.begin(), username.end(), '\n'), username.end());
    username.erase(std::remove(username.begin(), username.end(), '\r'), username.end());

    if (username.empty()) {
      safeClientSend(client, "Invalid username. Disconnecting.\n");
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(clients_mutex);
      for (const auto& existing_client : clients) {
        if (existing_client != client && 
            existing_client->active.load() && 
            existing_client->username == username) {
          safeClientSend(client, "Username already taken. Disconnecting.\n");
          return false;
        }
      }
    }

    {
      std::lock_guard<std::mutex> client_lock(client->client_mutex);
      client->username = username;
    }
    return true;
  }

  void sendWelcomeMessages(std::shared_ptr<Client> client) {
    safeClientSend(client, "\n=== Welcome to the Chat Server ===\n");
    safeClientSend(client, "Commands:\n");
    safeClientSend(client, "  /users  - List all connected users\n");
    safeClientSend(client, "  /quit   - Leave the chat\n");
    safeClientSend(client, "  /help   - Show this help\n");
    safeClientSend(client, "Just type your message to chat with everyone!\n\n");
  }

  void sendChatHistory(std::shared_ptr<Client> client) {
    std::lock_guard<std::mutex> guard(history_mutex);
    for (const auto& [username, message] : history) {
      safeClientSend(client, "[" + username + "]: " + message + "\n");
    }
  }

  void addToHistory(const std::string& username, const std::string& message) {
    std::lock_guard<std::mutex> guard(history_mutex);
    history.emplace_back(username, message);
    if (history.size() > 5) {
      history.erase(history.begin());
    }
  }

  void handleCommand(std::shared_ptr<Client> client, const std::string& command) {
    if (command == "/quit") {
      safeClientSend(client, "Goodbye!\n");
      disconnectClient(client);
    } else if (command == "/users") {
      std::string user_list = "Connected users:\n";
      {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (const auto& c : clients) {
          if (c->active.load() && !c->username.empty()) {
            user_list += "  - " + c->username + "\n";
          }
        }
      }
      safeClientSend(client, user_list);
    } else if (command == "/help") {
      sendWelcomeMessages(client);
    } else {
      safeClientSend(client, "Unknown command. Type /help for available commands.\n");
    }
  }

  bool safeClientSend(std::shared_ptr<Client> client, const std::string& message) {
    std::lock_guard<std::mutex> client_lock(client->client_mutex);
    
    if (!client->active.load()) {
      return false;
    }

    int sock = client->socket.load();
    if (sock == -1) {
      return false;
    }

    ssize_t sent = send(sock, message.c_str(), message.length(), MSG_NOSIGNAL);
    if (sent == -1) {
      client->active.store(false);
      return false;
    }
    return true;
  }

  void broadcastMessage(const std::string& message, std::shared_ptr<Client> exclude) {
    std::cout << message << std::endl;

    std::vector<std::shared_ptr<Client>> active_clients;
    {
      std::lock_guard<std::mutex> lock(clients_mutex);
      for (const auto& client : clients) {
        if (client != exclude && client->active.load() && !client->username.empty()) {
          active_clients.push_back(client);
        }
      }
    }

    for (const auto& client : active_clients) {
      safeClientSend(client, message + "\n");
    }
  }
};

ChatServer* g_server = nullptr;

void signalHandler(int signal) {
  if (g_server) {
    std::cout << "\nReceived signal " << signal << ". Shutting down...\n";
    g_server->shutdown();
  }
  exit(0);
}

int main() {
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  ChatServer server;
  g_server = &server;

  if (!server.start()) {
    std::cerr << "Failed to start server\n";
    return 1;
  }

  server.run();
  return 0;
}
