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
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean run