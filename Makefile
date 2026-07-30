# Compiler and Flags
CC = clang
AR = ar
SANITIZERS =
CFLAGS = -Wall -Wextra -Iinclude -g $(SANITIZERS) -O0 -fno-omit-frame-pointer
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

#find whitespace separated words in $(APP_SRCS) that match $(SRC_DIR)/%.c and replace the % in $(OBJ_DIR)/%.o with the text in $(APP_SRCS) matching the % in $(SRC_DIR)/%.c.
APP_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(APP_SRCS))	

TARGET     = mos_storage          # executable
LIB_TARGET = build/libmos.a

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/mos_test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(TEST_DIR)/%, $(TEST_SRCS))

FUZZY_SRCS = $(wildcard $(TEST_DIR)/mos_fuzzy_*.c)
FUZZY_BINS = $(patsubst $(TEST_DIR)/mos_fuzzy_%.c, $(TEST_DIR)/mos_fuzzy_%, $(FUZZY_SRCS))

# Default rule
dev_build: $(TARGET)

# Links all app object files (including main.o) into the main executable.
$(TARGET): $(APP_OBJS)
	$(CC) $(SANITIZERS) $(APP_OBJS) -o $(TARGET) $(LFLAGS)

$(LIB_TARGET): $(LIB_OBJS) | $(OBJ_DIR)
	$(AR) rcs $@ $^

# Compile source files to object files
# Pattern rule: compile any src/X.c → build/X.o. The | $(OBJ_DIR) is an order-only prerequisite — ensures build/ exists first without triggering rebuilds.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

lib: $(LIB_TARGET)

# Rule to build and run tests
# This compiles each .c file in /tests into its own executable
test: SANITIZERS = -fsanitize=address,undefined
test: CFLAGS_TEST += $(SANITIZERS)
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

$(TEST_DIR)/mos_test_%: $(TEST_DIR)/mos_test_%.c $(LIB_OBJS) tests/unity.c
	$(CC) $(CFLAGS_TEST) $^ -o $@

# Rule to build and run fuzzy tests
# This compiles each .c file starting with mos_fuzzy in /tests into its own executable
fuzzy: SANITIZERS = -fsanitize=fuzzer,address,undefined
fuzzy: CFLAGS_TEST += $(SANITIZERS)
fuzzy: $(LIB_OBJS) $(FUZZY_BINS)
	@echo "---------------------------------------"
	@echo "STARTING ALL FUZZY TESTS"
	@echo "---------------------------------------"
	@for test in $(FUZZY_BINS); do \
		echo "Running $$test..."; \
		./$$test; \
		echo ""; \
	done
	@echo "---------------------------------------"
	@echo "ALL FUZZY TEST FILES EXECUTED"
	@echo "---------------------------------------"

$(TEST_DIR)/mos_fuzzy_%: $(TEST_DIR)/mos_fuzzy_%.c $(LIB_OBJS)
	$(CC) $(CFLAGS_TEST) $^ -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	rm -f *.db
	rm -f ./tests/*.db
	rm -f $(TEST_BINS)
	rm -f $(FUZZY_BINS)
	rm -f ./tests/*.exe

debug: SANITIZERS = -fsanitize=address,undefined
debug: CFLAGS += $(SANITIZERS)
debug: CFLAGS_TEST += $(SANITIZERS)
debug: $(LIB_OBJS) $(TEST_BINS)

.PHONY: dev_build clean test fuzzy debug lib