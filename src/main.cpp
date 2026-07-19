#ifndef CLIENT
#include "core/Networking/FSServer/FSServer.h"
#include <csignal>
#endif
#ifdef CLIENT
#include "core/Networking/FSClient/FSClient.h"
#endif
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#ifndef CLIENT
std::atomic_bool running = true;
void sigHandler(const int sig) {
  switch (sig) {
  case SIGINT:
    running.store(false);
    break;
  }
}
#endif
int main(void) {
  try {
#ifndef CLIENT
    signal(SIGINT, sigHandler);
    FSServer sv = FSServer(running);
    sv.run();
#endif
#ifdef CLIENT
    FSClient c = FSClient();
    c.run();
#endif
  } catch (std::runtime_error &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    perror("Error \n");
    return -1;
  }

  return 0;
}
