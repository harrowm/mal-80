# Makefile for Mal-80 TRS-80 Emulator (macOS M4)

CXX = clang++
CXXSTD = -std=c++20
OPT = -O0 -g
WARN = -Wall -Wextra

SRC_DIR = src
BUILD_DIR = build
TARGET = mal-80

SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)

SOURCES = $(shell find $(SRC_DIR) -name '*.cpp')
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

CXXFLAGS = $(CXXSTD) $(OPT) $(WARN) $(SDL_CFLAGS) -arch arm64
LDFLAGS = $(SDL_LIBS) -arch arm64

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/cpu
	mkdir -p $(BUILD_DIR)/system
	mkdir -p $(BUILD_DIR)/video

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET) zexall_test

# ============================================================================
# ZEXALL Z80 Test Runner
# ============================================================================
TEST_DIR = tests/zexall
TEST_BUILD_DIR = $(BUILD_DIR)/tests/zexall
TEST_TARGET = zexall_test

# Test sources: test harness + Z80 CPU + Bus (no SDL, no Display)
TEST_SOURCES = $(TEST_DIR)/main.cpp $(SRC_DIR)/cpu/z80.cpp $(SRC_DIR)/system/Bus.cpp
TEST_OBJECTS = $(TEST_BUILD_DIR)/main.o $(TEST_BUILD_DIR)/z80.o $(TEST_BUILD_DIR)/Bus.o
TEST_CXXFLAGS = $(CXXSTD) -O2 -g $(WARN) -arch arm64

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

$(TEST_BUILD_DIR)/main.o: $(TEST_DIR)/main.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/z80.o: $(SRC_DIR)/cpu/z80.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/Bus.o: $(SRC_DIR)/system/Bus.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_OBJECTS)
	$(CXX) $(TEST_OBJECTS) -o $@ -arch arm64

# Download ZEXALL/ZEXDOC binaries from mdfs.net (CP/M zip archive)
ZEXALL_URL = https://mdfs.net/Software/Z80/Exerciser/CPM.zip

$(TEST_DIR)/zexall.com $(TEST_DIR)/zexdoc.com:
	@echo "Downloading Z80 Exerciser binaries..."
	@mkdir -p $(TEST_DIR)
	curl -L -o /tmp/cpm_zex.zip $(ZEXALL_URL)
	unzip -o /tmp/cpm_zex.zip zexall.com zexdoc.com -d $(TEST_DIR)
	@rm -f /tmp/cpm_zex.zip
	@echo "Downloaded zexall.com and zexdoc.com"

zexall: $(TEST_TARGET) $(TEST_DIR)/zexall.com
	./$(TEST_TARGET) $(TEST_DIR)/zexall.com

zexdoc: $(TEST_TARGET) $(TEST_DIR)/zexdoc.com
	./$(TEST_TARGET) $(TEST_DIR)/zexdoc.com

.PHONY: all clean run zexall zexdoc