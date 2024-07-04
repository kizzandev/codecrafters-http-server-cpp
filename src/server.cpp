#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  return elems;
}

std::string get_path(std::string &request) {
  return split(split(request, "\r\n"), ' ')[1];
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible
  // when running tests.
  std::cout << "Logs from your program will appear here!\n";

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect...\n";

  int client = accept(server_fd, (struct sockaddr *)&client_addr,
                      (socklen_t *)&client_addr_len);
  std::cout << "Client connected\n";

  // We initialize the buffer with 1024 bytes
  std::string request(1024, '\0');
  // recv will return -1 if there is an error
  // Otherwise it will return the number of bytes received
  size_t brecv = recv(client, &request[0], 1024, 0);
  if (brecv < 0) {
    std::cerr << "recv failed\n";
    return 1;
  }

  std::cout << "Request: " << request << "\n";
  bool isPathFound = true;

  std::string response;
  // MARK: Handle requests
  if (request.starts_with("GET")) {
    std::string path = get_path(request);
    std::cout << "Path: " << path << "\n";
    std::vector<std::string> path_parts = split(path, '/');
    if (path == "/")
      response = "HTTP/1.1 200 OK\r\n\r\n";
    else if (path_parts[1] == "echo") {
      // Status
      response = "HTTP/1.1 200 OK\r\n";
      // Headers
      response += "Content-Type: text/plain\r\n";
      response += "Content-Length: ";
      response += std::to_string(path_parts[2].size());
      response += "\r\n\r\n";
      // Body
      response += path_parts[2];
    } else {
      isPathFound = false;
    }
  }

  if (!isPathFound) {
    response = "HTTP/1.1 404 Not Found\r\n\r\n";
  }

  // Send the response
  send(client, response.c_str(), response.size(), 0);

  close(server_fd);

  return 0;
}
