CC = cc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c17 -O2
DEBUG_CFLAGS = -Wall -Wextra -Werror -pedantic -std=c17 -g -O0 -fsanitize=address,undefined -DDEBUG

SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEBUG_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%-debug.o,$(SRCS))

TARGET = splash
DEBUG_TARGET = splash-debug

# Test sources (exclude main.c from shell objects when linking tests)
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c,$(SRCS))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
DEBUG_LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%-debug.o,$(LIB_SRCS))
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

.PHONY: all debug test integration-test test-all clean

all: $(TARGET)

debug: $(DEBUG_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(DEBUG_TARGET): $(DEBUG_OBJS)
	$(CC) $(DEBUG_CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%-debug.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Tests: link test source with debug library objects
$(BUILD_DIR)/test_%: $(TEST_DIR)/test_%.c $(DEBUG_LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -I$(SRC_DIR) -o $@ $^

test: $(TEST_BINS)
	@echo "=== Running unit tests ==="
	@failed=0; \
	for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		$$t || failed=1; \
	done; \
	if [ $$failed -eq 1 ]; then \
		echo "=== SOME TESTS FAILED ==="; \
		exit 1; \
	else \
		echo "=== ALL TESTS PASSED ==="; \
	fi

INTEGRATION_DIR = $(TEST_DIR)/integration
INTEGRATION_TESTS = $(sort $(wildcard $(INTEGRATION_DIR)/test_*.sh))

integration-test: $(TARGET)
	@echo "=== Running integration tests ==="
	@failed=0; total=0; \
	for t in $(INTEGRATION_TESTS); do \
		total=$$((total + 1)); \
		echo "--- $$t ---"; \
		bash $$t || failed=$$((failed + 1)); \
	done; \
	echo ""; \
	if [ $$failed -ne 0 ]; then \
		echo "=== INTEGRATION: $$failed/$$total SCRIPTS FAILED ==="; \
		exit 1; \
	else \
		echo "=== INTEGRATION: ALL $$total SCRIPTS PASSED ==="; \
	fi

test-all: test integration-test

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(DEBUG_TARGET)
