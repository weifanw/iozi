CXX ?= c++
PYTHON ?= python3
BUILD_DIR ?= build
CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
HEADERS := $(wildcard include/iozi/*.hpp)

.PHONY: all test sanitize demo clean
all: $(BUILD_DIR)/iozi

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/iozi: src/main.cpp src/corpus.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/corpus.cpp $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_engine: tests/test_engine.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_engine.cpp $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_corpus: tests/test_corpus.cpp src/corpus.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_corpus.cpp src/corpus.cpp $(LDFLAGS) $(LDLIBS) -o $@

test: all $(BUILD_DIR)/test_engine $(BUILD_DIR)/test_corpus
	$(BUILD_DIR)/test_engine
	$(BUILD_DIR)/test_corpus
	$(PYTHON) tests/test_cli.py $(BUILD_DIR)/iozi

sanitize:
	$(MAKE) BUILD_DIR=build-sanitize CXXFLAGS="-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined" test

demo: all
	$(BUILD_DIR)/iozi --data examples/documents --query "search engine" --top 3

clean:
	rm -rf build build-sanitize
