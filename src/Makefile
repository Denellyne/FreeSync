CXX = g++
CFLAGS =-O2 -march=native -funroll-loops -flto -fopt-info-vec -ffast-math -Wall -Wextra -Wpedantic -std=c++23 -lssl -lz -lcrypto

INCLUDES := $(shell find . -name '*.cpp')
OBJ := $(patsubst %.cpp,%.o,$(INCLUDES))
TEST := *.md
DEBUG = -g -Wall -Wextra -pedantic
OUT = ../bin/main
CORES = ./cores/core.*

all: run
cr : clean run

build: $(OBJ)
	@echo "Linking..."
	$(CXX) $(CFLAGS) $(OBJ) -o $(OUT) 
	

%.o: %.cpp 
	@echo "Compiling..."
	$(CXX) $(CFLAGS) -c $< -o $@  	
	@echo "Compiled:" $@

run: $(OBJ)
	@echo "Linking..."
	$(CXX) $(CFLAGS) $(OBJ) -o $(OUT) 
	@$(OUT)

debug: 
	@echo "Compiling..."
	$(CXX) $(DEBUG) $(CFLAGS) $(INCLUDES) -o $(OUT)

dr:
	@echo "Compiling..."
	$(CXX) $(DEBUG) $(CFLAGS) $(INCLUDES) -o $(OUT)
	@$(OUT)

clean:
	@echo "Deleting..."
	@rm -f $(OBJ) $(OUT) $(TEST) $(CORES)
