#include "core/FTP/FTP.h"
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
std::atomic_bool running = true;
void sigHandler(const int sig) {
  switch (sig) {
  case SIGINT:
    running.store(false);
    break;
  }
}
int main(int argc, char *argv[]) {
  signal(SIGINT, sigHandler);
  try {
    FTP sv = FTP(running);
    sv.run();
  } catch (std::runtime_error &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    perror("Error \n");
    return -1;
  }

  return 0;
}
