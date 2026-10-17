#include "core/Node/Tree.h"
#include <fcntl.h>
// #include "core/Node/LTree.h"
// #include <fstream>
#include <unistd.h>
#ifndef CLIENT
#include "core/Networking/FSServer/FSServer.h"
#include <csignal>
std::atomic_bool running = true;
void sigHandler(const int sig) noexcept {
  switch (sig) {
  case SIGINT:
    running.store(false);
    break;
  }
}
int main(void) {

  // const auto current = Node::getHeadFile();
  // std::array<char, 64> arr;
  // memcpy(arr.data(), current.value().data(), 64);
  // LTree tree(arr, "/FreeSync", true);
  // std::vector<unsigned char> data;
  // std::ifstream file("LICENSE", std::ifstream::binary);
  // while (file.good())
  //   data.emplace_back(file.get());
  // std::cout << tree.getHash() << '\n';
  // std::cout << tree.addFile(data, "/FreeSync/LICENSE", false).value() <<
  // '\n'; bool a = tree.writeMerkleTree();

  Tree tree = Tree(".", true);
  if (!tree.writeMerkleTree()) {
    std::println("Unable to save merkle tree");
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, sigHandler);
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
int main(void) {
  FSClient client = FSClient();
  client.run();

  return 0;
}

#endif
