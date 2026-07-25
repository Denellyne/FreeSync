#include <fcntl.h>
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
