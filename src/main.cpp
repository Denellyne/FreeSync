#ifndef CLIENT
#include "core/Networking/FSServer/FSServer.h"
#include "core/Node/Tree.h"
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
std::atomic_bool running = true;
void sigHandler(const int sig) noexcept {
  switch (sig) {
  case SIGINT:
    running.store(false);
    break;
  }
}
int main(void) {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, sigHandler);
  if (const auto current = Node::getHeadFile(); !current.has_value()) {
    std::println("Unable to get head file");
    return 1;
  } else if (current.value() == "NOPARENT") {
    Tree tree = Tree(".", true);
    if (!tree.writeMerkleTree()) {
      std::println("Unable to save merkle tree");
      return 1;
    }
  }

  FSServer sv = FSServer(running);
  if (!sv) {
    std::println("Unable to initialize server, exiting...");
    return 1;
  }
  sv.run();
  return 0;
}
#endif

#ifdef CLIENT
#include "core/Networking/FSClient/FSClient.h"
int main(int argc, char *argv[]) {
  if (argc > 1) {
    FSClient client = FSClient(argv[1]);
    client.run();
    return 0;
  }
  FSClient client = FSClient();
  client.run();

  return 0;
}

#endif
// do huffman exchange for aes keys
