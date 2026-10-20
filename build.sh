#!/bin/bash
rm src/main.o || true
if [[ -n $1 ]]; then
  cd src && make -j"$(nproc)" CFLAGS_CMD="-DCLIENT" OUT="../bin/mainc" build && cd .. && clear && ./bin/mainc
else
  cd src && make -j"$(nproc)"  build && cd .. && clear && ./bin/main
fi
