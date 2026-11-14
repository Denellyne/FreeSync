#!/bin/bash
rm src/main.o || true
if [[ -n $1 ]]; then
   make -C src -j"$(nproc)" CFLAGS_CMD="-DCLIENT" OUT="../bin/mainc" build 
else
   make -C src -j"$(nproc)"  build 
fi
