TARGET_DIR := target

BUILD_DIR := $(TARGET_DIR)/build
LIB_NAME := libclp.a
BUILD_LIB := $(BUILD_DIR)/lib/$(LIB_NAME)

C_LIB_DIR  := libs/c_lib
C_LIB      := $(C_LIB_DIR)/target/build/lib/libd_lib.a

SRC_DIRS := src
SRCS := $(shell find $(SRC_DIRS) -name '*.c')
OBJS := $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

TEST_DIRS  := $(shell find tests -mindepth 1 -maxdepth 1 -type d 2>/dev/null)
SRCS_TEST  := $(foreach d,$(TEST_DIRS),$(d)/test.c)
OBJS_TESTS := $(addprefix $(TARGET_DIR)/, $(SRCS_TEST:.c=.o))
BIN_TESTS  := $(OBJS_TESTS:.o=)

DEPS := $(OBJS:.o=.d) $(OBJS_TESTS:.o=.d)

INC_DIRS := ./includes
INC_DIRS += $(shell find $(SRC_DIRS) -type d)
INC_DIRS += $(C_LIB_DIR)/includes
INC_DIRS += $(C_LIB_DIR)/vendor/wyhash
INC_DIRS += $(shell find $(C_LIB_DIR)/src -type d)

INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS := $(INC_FLAGS) -g3 -MMD -MP -Wall -Werror -Wextra


$(BUILD_LIB): $(OBJS)
	mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DNDEBUG=1 $(CFLAGS) -c $< -o $@

$(C_LIB):
	$(MAKE) -C $(C_LIB_DIR)

tests: $(C_LIB) $(BIN_TESTS)
.PHONY: tests

$(BIN_TESTS): %: %.o $(BUILD_LIB) $(C_LIB)
	mkdir -p $(dir $@)
	$(CC) $< $(BUILD_LIB) $(C_LIB) -o $@

$(TARGET_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(INC_FLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(TARGET_DIR)

-include $(DEPS)