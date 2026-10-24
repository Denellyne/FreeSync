CXX = g++
CFLAGS = -O3 -ftree-vectorize -fvect-cost-model=dynamic -march=native -fno-exceptions  -flto  -ffunction-sections -fdata-sections -fvisibility=hidden -fno-semantic-interposition -falign-functions=32 -pthread -falign-loops=32 -Wall -Wextra -Wpedantic -std=c++26 -lz-ng -lssl -lcrypto -lgtest -lgtest_main -flto-compression-level=1 -Wl,--gc-sections -Wl,-O1 -fno-plt -fno-math-errno -Wl,--as-needed -Wl,-z,now

INCLUDES := $(shell find ../src/core/ -name '*.cpp') $(shell find . -name '*.cpp')
OBJ := $(patsubst %.cpp,%.o,$(INCLUDES))
OUT = ./bin/testRunner

all: build run
cr : clean run

mkDir:
	@mkdir -p ./bin


build: mkDir $(OBJ)
	@echo "Linking..."
	$(CXX) $(CFLAGS_CMD) $(CFLAGS) $(OBJ) -o $(OUT) 
	

%.o: %.cpp 
	@echo "Compiling..."
	$(CXX) $(CFLAGS_CMD) $(CFLAGS) -c $< -o $@  	
	@echo "Compiled:" $@

run: build $(OBJ)
	@$(OUT)
	@rm -r ./bin

clean:
	@echo "Deleting..."
	@rm -f $(OBJ) $(OUT) $(TEST) $(CORES)
