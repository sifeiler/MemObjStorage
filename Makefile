# Compiler and Flags
CC = clang
AR = ar
SANITIZERS =

CFLAGS_COMMON = -Wall -Wextra -Iinclude -g $(SANITIZERS) -fno-omit-frame-pointer

CFLAGS = $(CFLAGS_COMMON) -O0 -mavx2 -mfma -g
CFLAGS_TEST = $(CFLAGS_COMMON) -O0 -mavx2 -mfma -g
CFLAGS_RELEASE = -Wall -Wextra -Iinclude -O3 -march=native -mavx2 -mfma -DNDEBUG -g -fno-omit-frame-pointer -fno-optimize-sibling-calls

# Directories
SRC_DIR = src
OBJ_DIR = build
OBJ_DIR_RELEASE = build-release
TEST_DIR = tests
INCLUDE_DIR = include

# Logic files (excluding main.c for testing purposes)
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c, $(wildcard $(SRC_DIR)/*.c))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(LIB_SRCS))
LIB_OBJS_RELEASE = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR_RELEASE)/%.o, $(LIB_SRCS))

# Main App files
APP_SRCS = $(wildcard $(SRC_DIR)/*.c)
APP_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(APP_SRCS))
APP_OBJS_RELEASE = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR_RELEASE)/%.o, $(APP_SRCS))

TARGET         = mos_storage
TARGET_RELEASE = mos_storage_release
LIB_TARGET         = build/libmos.a
LIB_TARGET_RELEASE = build-release/libmos.a

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/mos_test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(TEST_DIR)/%, $(TEST_SRCS))

FUZZY_SRCS = $(wildcard $(TEST_DIR)/mos_fuzzy_*.c)
FUZZY_BINS = $(patsubst $(TEST_DIR)/mos_fuzzy_%.c, $(TEST_DIR)/mos_fuzzy_%, $(FUZZY_SRCS))

# Default rule
dev_build: $(TARGET)

$(TARGET): $(APP_OBJS)
	$(CC) $(SANITIZERS) $(APP_OBJS) -o $(TARGET) $(LFLAGS)

$(LIB_TARGET): $(LIB_OBJS) | $(OBJ_DIR)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

lib: $(LIB_TARGET)

# ── Release build — own object dir, no sanitizers, own binary/lib names ────
$(TARGET_RELEASE): $(APP_OBJS_RELEASE)
	$(CC) $(APP_OBJS_RELEASE) -o $(TARGET_RELEASE) $(LFLAGS)

$(LIB_TARGET_RELEASE): $(LIB_OBJS_RELEASE) | $(OBJ_DIR_RELEASE)
	$(AR) rcs $@ $^

$(OBJ_DIR_RELEASE)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR_RELEASE)
	$(CC) $(CFLAGS_RELEASE) -c $< -o $@

$(OBJ_DIR_RELEASE):
	mkdir -p $(OBJ_DIR_RELEASE)

release: $(TARGET_RELEASE)
lib-release: $(LIB_TARGET_RELEASE)

# Rule to build and run tests
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

fuzzy-one: SANITIZERS = -fsanitize=fuzzer,address,undefined
fuzzy-one: CFLAGS_TEST += $(SANITIZERS)
fuzzy-one: $(LIB_OBJS) $(TEST_DIR)/$(TEST)
	./$(TEST_DIR)/$(TEST)

$(TEST_DIR)/mos_fuzzy_%: $(TEST_DIR)/mos_fuzzy_%.c $(LIB_OBJS)
	$(CC) $(CFLAGS_TEST) $^ -o $@

clean:
	rm -rf $(OBJ_DIR) $(OBJ_DIR_RELEASE) $(TARGET) $(TARGET_RELEASE)
	rm -f *.db
	rm -f ./tests/*.db
	rm -f $(TEST_BINS)
	rm -f $(FUZZY_BINS)
	rm -f ./tests/*.exe

debug: SANITIZERS = -fsanitize=address,undefined
debug: $(LIB_OBJS) $(TEST_BINS)

.PHONY: dev_build clean test fuzzy debug lib release lib-release fuzzy-one