#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

std::vector<std::string> split(const std::string &s, const char delim) {
  std::vector<std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  return elems;
}

struct Request {
  std::string method;
  std::string path;
  std::string http_version;
  std::vector<std::string> headers;
  std::string body;

  static Request parse(const std::string &request) {
    Request req;
    auto lines = split(request, '\r');
    auto req_line = split(lines[0], ' ');
    req.method = req_line[0];
    req.path = req_line[1];
    req.http_version = req_line[2];
    for (auto i = 1; i < lines.size() - 1; ++i) {
      req.headers.push_back(lines[i]);
    }
    req.body = lines[lines.size() - 1];
    return req;
  }
};

class Server {
 public:
  Server(int port) : m_port(port) { m_server_fd = setup_server(); };
  Server(int port, const std::string &directory)
      : m_port(port), m_directory(directory) {
    m_server_fd = setup_server();
  };
  ~Server() { close(m_server_fd); };

  void run() {
    while (true) {
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = sizeof(client_addr);
      int client = accept(m_server_fd, (struct sockaddr *)&client_addr,
                          (socklen_t *)&client_addr_len);
      if (client < 0) {
        std::cerr << "accept failed\n";
        break;
      }
      handle_client(client);
      close(client);
    }
  }

 private:
  int m_port;
  int m_server_fd;
  std::string m_directory;
  std::vector<std::string> m_allowed_methods = {"GET", "POST"};

 private:
  // Allowed methods: GET, POST
  bool allowed_method(const Request &request) {
    for (const std::string &method : m_allowed_methods) {
      if (request.method == method) {
        return true;
      }
    }
    return false;
  }

  int setup_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      std::cerr << "Failed to create server socket\n";
      exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
        0) {
      std::cerr << "setsockopt failed\n";
      exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
        0) {
      std::cerr << "Failed to bind to port\n";
      exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) != 0) {
      std::cerr << "Failed to listen\n";
      exit(EXIT_FAILURE);
    }

    return server_fd;
  }

  void handle_client(int client_fd) {
    std::string request_str(1024, '\0');
    size_t brecv = recv(client_fd, &request_str[0], 1024, 0);
    if (brecv < 0) {
      std::cerr << "recv failed\n";
      exit(EXIT_FAILURE);
    }

    Request request = Request::parse(request_str);
    std::string response = handle_request(request);
    send(client_fd, response.c_str(), response.size(), 0);
  }

  std::string handle_request(const Request &request) {
    std::string response;

    if (!allowed_method(request)) {
      return "HTTP/1.1 404 Not Found\r\n\r\n";
    }

    std::vector<std::string> paths = split(request.path, '/');
    if (request.path == "/") {
      response = "HTTP/1.1 200 OK\r\n\r\n";
    } else if (paths[1] == "echo") {
      // Status
      response = "HTTP/1.1 200 OK\r\n";
      // Headers
      response += "Content-Type: text/plain\r\n";
      response += "Content-Length: ";
      response += std::to_string(paths[2].size());
      response += "\r\n\r\n";
      // Body
      response += paths[2];
    } else if (paths[1] == "user-agent") {
      for (const std::string &header : request.headers) {
        if (header.find("User-Agent:") != std::string::npos) {
          std::string agent = split(header, ':')[1].substr(1);
          // Status
          response = "HTTP/1.1 200 OK\r\n";
          // Headers
          response += "Content-Type: text/plain\r\n";
          response += "Content-Length: ";
          response += std::to_string(agent.size());
          response += "\r\n\r\n";
          // Body
          response += agent;
          break;
        }
      }
      if (response.empty()) {
        response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Length: 0\r\n\r\n";
      }
    } else if (paths[1] == "files" && request.method == "GET") {
      if (m_directory.empty() || paths.size() < 3) {
        response = "HTTP/1.1 404 Not Found\r\n\r\n";
        return response;
      }

      std::string filename = paths[2];
      std::ifstream file(m_directory + "/" + filename,
                         std::ios::binary | std::ios::ate);
      if (!file.is_open()) {
        response = "HTTP/1.1 404 Not Found\r\n\r\n";
      } else {
        response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: application/octet-stream\r\n";
        response += "Content-Length: ";
        response += std::to_string(file.tellg());
        response += "\r\n\r\n";
        file.seekg(0, std::ios::beg);
        std::string body((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        response += body;
      }
    } else if (paths[1] == "files" && request.method == "POST") {
      if (m_directory.empty() || paths.size() < 3) {
        response = "HTTP/1.1 404 Not Found\r\n\r\n";
        return response;
      }
      std::string filename = paths[2];
      std::ofstream file(m_directory + "/" + filename, std::ios::binary);
      if (!file.is_open()) {
        response = "HTTP/1.1 404 Not Found\r\n\r\n";
      } else {
        response = "HTTP/1.1 201 OK\r\n\r\n";
        file << request.body;
      }
    } else {
      response = "HTTP/1.1 404 Not Found\r\n\r\n";
    }

    return response;
  }
};