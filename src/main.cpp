#include "core/FTP/FTP.h"
#include "core/Node/LTree.h"
#include "core/Node/Tree.h"
#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
int main(int argc, char *argv[]) {
  try {
    FTP sv = FTP();
    sv.run();

  } catch (std::runtime_error &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    perror("Error \n");
    return -1;
  }

  return 0;
}
