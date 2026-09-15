#include "core/Node/Tree.h"
#include <fcntl.h>
// #include "core/Node/LTree.h"
// #include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#ifndef CLIENT
#include "core/Networking/FSServer/FSServer.h"
#include <csignal>
std::atomic_bool running = true;
void sigHandler(const int sig) {
  switch (sig) {
  case SIGINT:
    running.store(false);
    break;
  }
}
int main(void) {
  try {

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

    signal(SIGINT, sigHandler);
    FSServer sv = FSServer(running);
    sv.run();
  } catch (std::runtime_error &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    perror("Error \n");
    return -1;
  }
  return 0;
}
#endif

#ifdef CLIENT
#include "core/Networking/FSClient/FSClient.h"
int main(void) {
  try {
    FSClient c = FSClient();
    c.run();
  } catch (std::runtime_error &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    perror("Error \n");
    return -1;
  }

  return 0;
}

#endif
