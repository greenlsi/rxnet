CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Iinclude
EXAMPLE_CFLAGS ?= $(CFLAGS) -Wno-unused-parameter
BUILD_DIR ?= build

LIB          := $(BUILD_DIR)/librxnet.a
LIB_OBJS     := $(BUILD_DIR)/runtime.o $(BUILD_DIR)/fsm.o $(BUILD_DIR)/pn.o \
                $(BUILD_DIR)/cyclic.o $(BUILD_DIR)/coop.o $(BUILD_DIR)/thread.o

FSM_00_LIGHT_BIN := $(BUILD_DIR)/fsm_00_light
FSM_01_AUTO_BIN  := $(BUILD_DIR)/fsm_01_auto
FSM_02_BLINK_BIN := $(BUILD_DIR)/fsm_02_blink
FSM_03_MIX_BIN        := $(BUILD_DIR)/fsm_03_mix
FSM_03_MIX_THREADS_BIN := $(BUILD_DIR)/fsm_03_mix_threads
FSM_03_MIX_COOP_BIN    := $(BUILD_DIR)/fsm_03_mix_coop
PN_00_LIGHT_BIN  := $(BUILD_DIR)/pn_00_light
PN_01_AUTO_BIN   := $(BUILD_DIR)/pn_01_auto
PN_02_BLINK_BIN  := $(BUILD_DIR)/pn_02_blink
PN_03_MIX_BIN         := $(BUILD_DIR)/pn_03_mix
PN_03_MIX_THREADS_BIN := $(BUILD_DIR)/pn_03_mix_threads
PN_03_MIX_COOP_BIN    := $(BUILD_DIR)/pn_03_mix_coop
MIXED_BIN        := $(BUILD_DIR)/mixed

# Shared sources reused by all PN CLI examples
PN_CLI_SHARED := examples/fsm/00-light/app_driver.c examples/fsm/00-light/cli_fsm.c
PN_CLI_FLAGS  := $(EXAMPLE_CFLAGS) -Iexamples/fsm/00-light

TEST_RUNTIME_BIN  := $(BUILD_DIR)/test_runtime
TEST_FSM_BIN      := $(BUILD_DIR)/test_fsm
TEST_PN_BIN       := $(BUILD_DIR)/test_pn
TEST_BINS         := $(TEST_RUNTIME_BIN) $(TEST_FSM_BIN) $(TEST_PN_BIN)

PARITY_RUNNER_BIN := $(BUILD_DIR)/parity_runner

.PHONY: all lib examples test parity clean

all: lib examples

# ------------------------------------------------------------------ #
# Static library                                                       #
# ------------------------------------------------------------------ #

lib: $(LIB)

$(LIB): $(LIB_OBJS)
	ar rcs $@ $^

$(BUILD_DIR)/runtime.o: src/runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fsm.o: src/fsm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pn.o: src/pn.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cyclic.o: src/cyclic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/coop.o: src/coop.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/thread.o: src/thread.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR):
	mkdir -p $@

# ------------------------------------------------------------------ #
# Examples                                                             #
# ------------------------------------------------------------------ #

examples: light_cli auto_cli blink_cli mix_cli mix_threads mix_coop \
          pn_00_light pn_01_auto pn_02_blink pn_03_mix pn_03_mix_threads pn_03_mix_coop \
          mixed

light_cli: $(FSM_00_LIGHT_BIN)

$(FSM_00_LIGHT_BIN): $(LIB) examples/fsm/00-light/app_driver.c examples/fsm/00-light/light_fsm.c examples/fsm/00-light/cli_fsm.c examples/fsm/00-light/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

auto_cli: $(FSM_01_AUTO_BIN)

$(FSM_01_AUTO_BIN): $(LIB) examples/fsm/01-auto/app_driver.c examples/fsm/01-auto/auto_fsm.c examples/fsm/01-auto/cli_fsm.c examples/fsm/01-auto/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

blink_cli: $(FSM_02_BLINK_BIN)

$(FSM_02_BLINK_BIN): $(LIB) examples/fsm/02-blink/app_driver.c examples/fsm/02-blink/blink_fsm.c examples/fsm/02-blink/cli_fsm.c examples/fsm/02-blink/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

mix_cli: $(FSM_03_MIX_BIN)

$(FSM_03_MIX_BIN): $(LIB) examples/fsm/03-mix/app_driver.c examples/fsm/03-mix/light_fsm.c examples/fsm/03-mix/blink_fsm.c examples/fsm/03-mix/auto_fsm.c examples/fsm/03-mix/cli_fsm.c examples/fsm/03-mix/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

MIX_SHARED := $(LIB) examples/fsm/03-mix/app_driver.c examples/fsm/03-mix/light_fsm.c examples/fsm/03-mix/blink_fsm.c examples/fsm/03-mix/auto_fsm.c examples/fsm/03-mix/cli_fsm.c
MIX_FLAGS  := $(EXAMPLE_CFLAGS) -Iexamples/fsm/03-mix

mix_threads: $(FSM_03_MIX_THREADS_BIN)

$(FSM_03_MIX_THREADS_BIN): $(MIX_SHARED) examples/fsm/03-mix/main_threads.c | $(BUILD_DIR)
	$(CC) $(MIX_FLAGS) $^ -o $@ -lpthread

mix_coop: $(FSM_03_MIX_COOP_BIN)

$(FSM_03_MIX_COOP_BIN): $(MIX_SHARED) examples/fsm/03-mix/main_coop.c | $(BUILD_DIR)
	$(CC) $(MIX_FLAGS) $^ -o $@


PN_MIX_SHARED := $(LIB) $(PN_CLI_SHARED) \
    examples/pn/00-light/light_pn.c \
    examples/pn/01-auto/auto_pn.c \
    examples/pn/02-blink/blink_pn.c
PN_MIX_FLAGS  := $(PN_CLI_FLAGS) \
    -Iexamples/pn/00-light \
    -Iexamples/pn/01-auto \
    -Iexamples/pn/02-blink

pn_03_mix_threads: $(PN_03_MIX_THREADS_BIN)

$(PN_03_MIX_THREADS_BIN): $(PN_MIX_SHARED) examples/pn/03-mix/main_threads.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) $^ -o $@ -lpthread

pn_03_mix_coop: $(PN_03_MIX_COOP_BIN)

$(PN_03_MIX_COOP_BIN): $(PN_MIX_SHARED) examples/pn/03-mix/main_coop.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) $^ -o $@

pn_00_light: $(PN_00_LIGHT_BIN)

$(PN_00_LIGHT_BIN): $(LIB) $(PN_CLI_SHARED) \
    examples/pn/00-light/light_pn.c \
    examples/pn/00-light/main_cli.c | $(BUILD_DIR)
	$(CC) $(PN_CLI_FLAGS) -Iexamples/pn/00-light $^ -o $@

pn_01_auto: $(PN_01_AUTO_BIN)

$(PN_01_AUTO_BIN): $(LIB) $(PN_CLI_SHARED) \
    examples/pn/01-auto/auto_pn.c \
    examples/pn/01-auto/main_cli.c | $(BUILD_DIR)
	$(CC) $(PN_CLI_FLAGS) -Iexamples/pn/01-auto $^ -o $@

pn_02_blink: $(PN_02_BLINK_BIN)

$(PN_02_BLINK_BIN): $(LIB) $(PN_CLI_SHARED) \
    examples/pn/02-blink/blink_pn.c \
    examples/pn/02-blink/main_cli.c | $(BUILD_DIR)
	$(CC) $(PN_CLI_FLAGS) -Iexamples/pn/02-blink $^ -o $@

pn_03_mix: $(PN_03_MIX_BIN)

$(PN_03_MIX_BIN): $(LIB) $(PN_CLI_SHARED) \
    examples/pn/00-light/light_pn.c \
    examples/pn/01-auto/auto_pn.c \
    examples/pn/02-blink/blink_pn.c \
    examples/pn/03-mix/main_cli.c | $(BUILD_DIR)
	$(CC) $(PN_CLI_FLAGS) \
	    -Iexamples/pn/00-light \
	    -Iexamples/pn/01-auto \
	    -Iexamples/pn/02-blink \
	    $^ -o $@

mixed: $(MIXED_BIN)

$(MIXED_BIN): $(LIB) $(PN_CLI_SHARED) \
    examples/fsm/00-light/light_fsm.c \
    examples/pn/00-light/light_pn.c \
    examples/mixed/main_cli.c | $(BUILD_DIR)
	$(CC) $(PN_CLI_FLAGS) \
	    -Iexamples/fsm/00-light \
	    -Iexamples/pn/00-light \
	    $^ -o $@

# ------------------------------------------------------------------ #
# Tests                                                                #
# ------------------------------------------------------------------ #

test: $(TEST_BINS)
	@echo ""
	@echo "Running test_runtime..."
	@$(TEST_RUNTIME_BIN)
	@echo ""
	@echo "Running test_fsm..."
	@$(TEST_FSM_BIN)
	@echo ""
	@echo "Running test_pn..."
	@$(TEST_PN_BIN)
	@echo ""
	@echo "All test suites passed."

$(TEST_RUNTIME_BIN): tests/test_runtime.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

$(TEST_FSM_BIN): tests/test_fsm.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

$(TEST_PN_BIN): tests/test_pn.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

$(PARITY_RUNNER_BIN): tests/parity_runner.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

parity: $(PARITY_RUNNER_BIN)

# ------------------------------------------------------------------ #
# Install                                                              #
# ------------------------------------------------------------------ #

PREFIX ?= /usr/local

install: $(LIB)
	install -d $(PREFIX)/lib
	install -m 644 $(LIB) $(PREFIX)/lib/
	install -d $(PREFIX)/include/rxnet
	install -m 644 include/rxnet/*.h $(PREFIX)/include/rxnet/


# ------------------------------------------------------------------ #
# Clean                                                                #
# ------------------------------------------------------------------ #

clean:
	rm -rf $(BUILD_DIR)
