#include "core/Networking/FSClient/CLI/FSClient.h"
int main(int argc, char *argv[]) {

  if (argc < 2) {
    std::println("You must pass the server ip");
    return 1;
  }

  FSClient client = FSClient(argv[1]);
  client.run();
  return 0;
}
