#include "server.hpp"

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // If there are arguments, check if they are:
  // --directory
  if (argc > 1) {
    std::string directory;
    for (int i = 1; i < argc; i++) {
      if (std::string(argv[i]) == "--directory") {
        if (i + 1 < argc) {
          directory = argv[i + 1];
          break;
        } else {
          std::cerr << "Usage: ./server [--directory <directory>]\n";
          return 1;
        }
      }
    }
    if (!directory.empty()) {
      Server server(4221, directory);
      server.run();
    }
  } else {
    Server server(4221);
    server.run();
  }
  return 0;
}
