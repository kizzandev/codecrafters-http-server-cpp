#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
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
    for (auto i = 1; i < lines.size() - 1; ++i) req.headers.push_back(lines[i]);

    req.body = lines[lines.size() - 1].substr(1);  // remove \n
    size_t lastNonZero = req.body.find_last_not_of('\x00');
    if (lastNonZero != std::string::npos) req.body.resize(lastNonZero + 1);

    return req;
  }
};

struct Response {
  std::string status;
  std::string headers;
  std::string body;
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
  bool allowed_method(const Request &request) {
    for (const std::string &method : m_allowed_methods) {
      if (request.method == method) {
        return true;
      }
    }
    return false;
  }

  void replace_header(Response &response, const std::string &header,
                      const std::string &value) {
    size_t pos = response.headers.find(header);
    if (pos != std::string::npos) {
      size_t end = response.headers.find("\r\n", pos);
      response.headers.erase(pos, end - pos + 2);
    }
    response.headers += header + ": " + value + "\r\n";
  }

  std::string compress_gzip(const std::string &body) {
    try {
      std::cout << "Size before gzip compression: " << body.size() << std::endl;

      z_stream stream{};
      stream.next_in =
          reinterpret_cast<Bytef *>(const_cast<char *>(body.c_str()));
      stream.avail_in = body.size();

      if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 | 16, 8,
                       Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib");
      }

      std::stringstream compressed;
      char buffer[32768];
      int ret;

      do {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof(buffer);
        ret = deflate(&stream, Z_FINISH);
        if (compressed.write(buffer, sizeof(buffer) - stream.avail_out)
                .fail()) {
          deflateEnd(&stream);
          throw std::runtime_error("Failed to write compressed data");
        }
      } while (ret == Z_OK);

      deflateEnd(&stream);

      if (ret != Z_STREAM_END) {
        throw std::runtime_error("Failed to compress data");
      }

      std::string compressed_str = compressed.str();
      std::cout << "Size after gzip compression: " << compressed_str.size()
                << std::endl;
      return compressed_str;

    } catch (const std::exception &e) {
      std::cerr << "Compression error: " << e.what() << std::endl;
      return body;
    }
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

  void handle_post(const Request &request, Response &response) {
    std::vector<std::string> paths = split(request.path, '/');

    if (paths[1] == "files") {
      if (m_directory.empty() || paths.size() < 3) {
        response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
        return;
      }
      std::string filename = paths[2];
      std::ofstream file(m_directory + "/" + filename, std::ios::binary);
      if (!file.is_open()) {
        response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
      } else {
        response = {.status = "HTTP/1.1 201 Created\r\n\r\n"};
        file << request.body;
      }
    } else {
      response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
    }
  }

  void handle_get(const Request &request, Response &response) {
    std::vector<std::string> paths = split(request.path, '/');

    if (request.path == "/") {
      response = {.status = "HTTP/1.1 200 OK\r\n\r\n"};
    } else if (paths[1] == "echo") {
      response = {.status = "HTTP/1.1 200 OK\r\n",
                  .headers = "Content-Type: text/plain\r\nContent-Length: " +
                             std::to_string(paths[2].size()) + "\r\n",
                  .body = paths[2]};
    } else if (paths[1] == "user-agent") {
      for (const std::string &header : request.headers) {
        if (header.find("User-Agent:") != std::string::npos) {
          std::string agent = split(header, ':')[1].substr(1);
          response = {
              .status = "HTTP/1.1 200 OK\r\n",
              .headers = "Content-Type: text/plain\r\nContent-Length: " +
                         std::to_string(agent.size()) + "\r\n",
              .body = agent};
          break;
        }
      }
      if (response.body.empty()) {
        response = {
            .status = "HTTP/1.1 200 OK\r\n",
            .headers = "Content-Type: text/plain\r\nContent-Length: 0\r\n"};
      }
    } else if (paths[1] == "files") {
      if (m_directory.empty() || paths.size() < 3) {
        response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
        return;
      }

      std::string filename = paths[2];
      std::ifstream file(m_directory + "/" + filename,
                         std::ios::binary | std::ios::ate);
      if (!file.is_open()) {
        response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
      } else {
        file.seekg(0, std::ios::beg);
        std::string body((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        response = {
            .status = "HTTP/1.1 200 OK\r\n",
            .headers =
                "Content-Type: application/octet-stream\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\n",
            .body = body};
      }
    } else {
      response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
    }

    for (const std::string &header : request.headers) {
      auto accept_encoding = header.find("Accept-Encoding:");
      if (accept_encoding != std::string::npos) {
        if (header.find("gzip") != std::string::npos) {
          response.headers += "Content-Encoding: ";
          response.headers += "gzip\r\n";
          response.body = compress_gzip(response.body);
          replace_header(response, "Content-Length",
                         std::to_string(response.body.size()));
        }
        break;
      }
    }

    response.headers += "\r\n";
  }

  std::string handle_request(const Request &request) {
    Response response;

    if (!allowed_method(request)) {
      response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
    }

    if (request.method == "POST") {
      handle_post(request, response);
    } else if (request.method == "GET") {
      handle_get(request, response);
    } else {
      response = {.status = "HTTP/1.1 404 Not Found\r\n\r\n"};
    }

    std::string response_str =
        response.status + response.headers + response.body;
    return response_str;
  }
};