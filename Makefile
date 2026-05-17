# Compiler and Flags
CC = clang
SANITIZERS = -fsanitize=address,undefined
CFLAGS = -Wall -Wextra -Iinclude -g $(SANITIZERS)
CFLAGS += -O0
CFLAGS += -fno-omit-frame-pointer
CFLAGS_TEST = -Iinclude -g $(SANITIZERS)

# Directories
SRC_DIR = src
OBJ_DIR = build
TEST_DIR = tests
INCLUDE_DIR = include

# Logic files (excluding main.c for testing purposes)
# We exclude main.c because tests usually have their own main()
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c, $(wildcard $(SRC_DIR)/*.c))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(LIB_SRCS))

# Main App files
APP_SRCS = $(wildcard $(SRC_DIR)/*.c)
APP_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(APP_SRCS))
TARGET = mos_storage

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/mos_test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(TEST_DIR)/%, $(TEST_SRCS))

# Default rule
dev_build: $(TARGET)

# Link the main executable
$(TARGET): $(APP_OBJS)
	$(CC) $(SANITIZERS) $(APP_OBJS) -o $(TARGET) $(LFLAGS)

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Rule to build and run tests
# This compiles each .c file in /tests into its own executable
test: $(LIB_OBJS) $(TEST_BINS)
	@echo "---------------------------------------"
	@echo "STARTING ALL TESTS"
	@echo "---------------------------------------"
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test; \
		echo ""; \
	done
	@echo "---------------------------------------"
	@echo "ALL TEST FILES EXECUTED"
	@echo "---------------------------------------"

$(TEST_DIR)/%: $(TEST_DIR)/%.c $(LIB_OBJS) tests/unity.c
	$(CC) $(SANITIZERS) $(CFLAGS_TEST) $^ -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	rm -f *.db
	rm -f ./tests/*.db
	rm -f ./tests/*.exe

debug: SANITIZERS =
debug: CFLAGS_TEST += -g -O0
debug: $(LIB_OBJS) $(TEST_BINS)

.PHONY: dev_build clean test debug