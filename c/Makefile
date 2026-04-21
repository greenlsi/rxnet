CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -Iinclude
# _POSIX_C_SOURCE must be defined before the first system header in every
# translation unit.  Use 'override' so this survives a command-line CFLAGS=…
override CFLAGS += -D_POSIX_C_SOURCE=200809L
EXAMPLE_CFLAGS ?= $(CFLAGS) -Wno-unused-parameter
TRACE_CFLAGS := $(CFLAGS) -DRX_TRACE_ENABLE
BUILD_DIR ?= build

# GNU ld requires static archives after the objects that reference them.
# link_src strips $(LIB) from $^ so recipes can append it explicitly at the end.
link_src = $(filter-out $(LIB),$^)
trace_link_src = $(filter-out $(TRACE_LIB),$^)

LIB            := $(BUILD_DIR)/librxnet.a
LIB_OBJS       := $(BUILD_DIR)/runtime.o $(BUILD_DIR)/fsm.o $(BUILD_DIR)/pn.o \
                  $(BUILD_DIR)/cyclic.o $(BUILD_DIR)/coop.o $(BUILD_DIR)/thread.o

# Trace-enabled library (compiled fresh with -DRX_TRACE_ENABLE)
TRACE_LIB      := $(BUILD_DIR)/librxnet_trace.a
TRACE_LIB_OBJS := $(BUILD_DIR)/runtime_trace.o $(BUILD_DIR)/fsm_trace.o $(BUILD_DIR)/pn_trace.o \
                  $(BUILD_DIR)/cyclic.o $(BUILD_DIR)/coop.o $(BUILD_DIR)/thread.o \
                  $(BUILD_DIR)/trace.o

FSM_01_LIGHT_BIN             := $(BUILD_DIR)/fsm_01_light
FSM_02_AUTO_BIN              := $(BUILD_DIR)/fsm_02_auto
FSM_03_BLINK_BIN             := $(BUILD_DIR)/fsm_03_blink
FSM_04_MIX_CYCLIC_BIN        := $(BUILD_DIR)/fsm_04_mix_cyclic
FSM_04_MIX_THREADS_BIN       := $(BUILD_DIR)/fsm_04_mix_threads
FSM_04_MIX_COOP_BIN          := $(BUILD_DIR)/fsm_04_mix_coop
FSM_04_MIX_CYCLIC_TRACE_BIN  := $(BUILD_DIR)/fsm_04_mix_cyclic_trace
FSM_04_MIX_THREADS_TRACE_BIN := $(BUILD_DIR)/fsm_04_mix_threads_trace
FSM_04_MIX_COOP_TRACE_BIN    := $(BUILD_DIR)/fsm_04_mix_coop_trace
PN_01_LIGHT_BIN              := $(BUILD_DIR)/pn_01_light
PN_02_AUTO_BIN               := $(BUILD_DIR)/pn_02_auto
PN_03_BLINK_BIN              := $(BUILD_DIR)/pn_03_blink
PN_04_MIX_CYCLIC_BIN         := $(BUILD_DIR)/pn_04_mix_cyclic
PN_04_MIX_THREADS_BIN        := $(BUILD_DIR)/pn_04_mix_threads
PN_04_MIX_COOP_BIN           := $(BUILD_DIR)/pn_04_mix_coop
PN_04_MIX_CYCLIC_TRACE_BIN   := $(BUILD_DIR)/pn_04_mix_cyclic_trace
PN_04_MIX_THREADS_TRACE_BIN  := $(BUILD_DIR)/pn_04_mix_threads_trace
PN_04_MIX_COOP_TRACE_BIN     := $(BUILD_DIR)/pn_04_mix_coop_trace
MIXED_BIN                    := $(BUILD_DIR)/mixed

TEST_RUNTIME_BIN  := $(BUILD_DIR)/test_runtime
TEST_FSM_BIN      := $(BUILD_DIR)/test_fsm
TEST_PN_BIN       := $(BUILD_DIR)/test_pn
TEST_TRACE_BIN    := $(BUILD_DIR)/test_trace
TEST_BINS         := $(TEST_RUNTIME_BIN) $(TEST_FSM_BIN) $(TEST_PN_BIN) $(TEST_TRACE_BIN)

PARITY_RUNNER_BIN := $(BUILD_DIR)/parity_runner

.PHONY: all lib examples test parity clean

all: lib examples

# ------------------------------------------------------------------ #
# Static library                                                     #
# ------------------------------------------------------------------ #

lib: $(LIB) $(TRACE_LIB)

$(LIB): $(LIB_OBJS)
	ar rcs $@ $^

$(TRACE_LIB): $(TRACE_LIB_OBJS)
	ar rcs $@ $^

$(BUILD_DIR)/trace.o: src/trace.c | $(BUILD_DIR)
	$(CC) $(TRACE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_trace.o: src/%.c | $(BUILD_DIR)
	$(CC) $(TRACE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

examples: fsm_01_light fsm_02_auto fsm_03_blink \
          fsm_04_mix_cyclic fsm_04_mix_threads fsm_04_mix_coop \
          fsm_04_mix_cyclic_trace fsm_04_mix_threads_trace fsm_04_mix_coop_trace \
          pn_01_light pn_02_auto pn_03_blink \
          pn_04_mix_cyclic pn_04_mix_threads pn_04_mix_coop \
          pn_04_mix_cyclic_trace pn_04_mix_threads_trace pn_04_mix_coop_trace \
          mixed

# ------------------------------------------------------------------ #
# FSM Examples                                                       #
# ------------------------------------------------------------------ #

fsm_01_light: $(FSM_01_LIGHT_BIN)

$(FSM_01_LIGHT_BIN): $(LIB) examples/fsm/01-light/app_driver.c examples/fsm/01-light/light_fsm.c examples/fsm/01-light/cli_fsm.c examples/fsm/01-light/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(link_src) $(LIB) -o $@

fsm_02_auto: $(FSM_02_AUTO_BIN)

$(FSM_02_AUTO_BIN): $(LIB) examples/fsm/01-light/app_driver.c examples/fsm/02-auto/auto_fsm.c examples/fsm/01-light/cli_fsm.c examples/fsm/02-auto/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) -Iexamples/fsm/01-light $(link_src) $(LIB) -o $@

fsm_03_blink: $(FSM_03_BLINK_BIN)

$(FSM_03_BLINK_BIN): $(LIB) examples/fsm/01-light/app_driver.c examples/fsm/03-blink/blink_fsm.c examples/fsm/01-light/cli_fsm.c examples/fsm/03-blink/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) -Iexamples/fsm/01-light $(link_src) $(LIB) -o $@

FSM_MIX_SHARED := \
    examples/fsm/01-light/app_driver.c \
    examples/fsm/01-light/light_fsm.c \
    examples/fsm/02-auto/auto_fsm.c \
    examples/fsm/03-blink/blink_fsm.c \
    examples/fsm/01-light/cli_fsm.c
FSM_MIX_FLAGS  := $(EXAMPLE_CFLAGS) \
    -Iexamples/fsm/03-blink \
    -Iexamples/fsm/02-auto \
    -Iexamples/fsm/01-light

fsm_04_mix_cyclic: $(FSM_04_MIX_CYCLIC_BIN)

$(FSM_04_MIX_CYCLIC_BIN): $(LIB) $(FSM_MIX_SHARED) examples/fsm/04-mix/main_cyclic.c | $(BUILD_DIR)
	$(CC) $(FSM_MIX_FLAGS) $(link_src) $(LIB) -o $@

fsm_04_mix_threads: $(FSM_04_MIX_THREADS_BIN)

$(FSM_04_MIX_THREADS_BIN): $(LIB) $(FSM_MIX_SHARED) examples/fsm/04-mix/main_threads.c | $(BUILD_DIR)
	$(CC) $(FSM_MIX_FLAGS) $(link_src) $(LIB) -o $@ -lpthread

fsm_04_mix_coop: $(FSM_04_MIX_COOP_BIN)

$(FSM_04_MIX_COOP_BIN): $(LIB) $(FSM_MIX_SHARED) examples/fsm/04-mix/main_coop.c | $(BUILD_DIR)
	$(CC) $(FSM_MIX_FLAGS) $(link_src) $(LIB) -o $@

fsm_04_mix_cyclic_trace: $(FSM_04_MIX_CYCLIC_TRACE_BIN)

$(FSM_04_MIX_CYCLIC_TRACE_BIN): $(TRACE_LIB) $(FSM_MIX_SHARED) examples/fsm/04-mix/main_cyclic_trace.c | $(BUILD_DIR)
	$(CC) $(FSM_MIX_FLAGS) -DRX_TRACE_ENABLE $(link_src) $(TRACE_LIB) -o $@

fsm_04_mix_threads_trace: $(FSM_04_MIX_THREADS_TRACE_BIN)

$(FSM_04_MIX_THREADS_TRACE_BIN): $(TRACE_LIB) $(FSM_MIX_SHARED) examples/fsm/04-mix/main_threads_trace.c | $(BUILD_DIR)
	$(CC) $(FSM_MIX_FLAGS) -DRX_TRACE_ENABLE $(link_src) $(TRACE_LIB) -o $@ -lpthread

fsm_04_mix_coop_trace: $(FSM_04_MIX_COOP_TRACE_BIN)

$(FSM_04_MIX_COOP_TRACE_BIN): $(TRACE_LIB) $(FSM_MIX_SHARED) examples/fsm/04-mix/main_coop_trace.c | $(BUILD_DIR)
	$(CC) $(FSM_MIX_FLAGS) -DRX_TRACE_ENABLE $(link_src) $(TRACE_LIB) -o $@

# ------------------------------------------------------------------ #
# PN Examples                                                       #
# ------------------------------------------------------------------ #
 
pn_01_light: $(PN_01_LIGHT_BIN)

$(PN_01_LIGHT_BIN): $(LIB) examples/fsm/01-light/app_driver.c examples/pn/01-light/light_pn.c examples/fsm/01-light/cli_fsm.c examples/pn/01-light/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) -Iexamples/fsm/01-light $(link_src) $(LIB) -o $@

pn_02_auto: $(PN_02_AUTO_BIN)

$(PN_02_AUTO_BIN): $(LIB) examples/fsm/01-light/app_driver.c examples/pn/02-auto/auto_pn.c examples/fsm/01-light/cli_fsm.c examples/pn/02-auto/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) -Iexamples/fsm/01-light $(link_src) $(LIB) -o $@

pn_03_blink: $(PN_03_BLINK_BIN)

$(PN_03_BLINK_BIN): $(LIB) examples/fsm/01-light/app_driver.c examples/pn/03-blink/blink_pn.c examples/fsm/01-light/cli_fsm.c examples/pn/03-blink/main_cli.c | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) -Iexamples/fsm/01-light $(link_src) $(LIB) -o $@

PN_MIX_SHARED := \
    examples/fsm/01-light/app_driver.c \
    examples/pn/01-light/light_pn.c \
    examples/pn/02-auto/auto_pn.c \
    examples/pn/03-blink/blink_pn.c \
    examples/fsm/01-light/cli_fsm.c

PN_MIX_FLAGS  := $(EXAMPLE_CFLAGS) \
    -Iexamples/pn/03-blink \
    -Iexamples/pn/02-auto \
    -Iexamples/pn/01-light \
    -Iexamples/fsm/01-light

pn_04_mix_cyclic: $(PN_04_MIX_CYCLIC_BIN)

$(PN_04_MIX_CYCLIC_BIN): $(LIB) $(PN_MIX_SHARED) examples/pn/04-mix/main_cyclic.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) $(link_src) $(LIB) -o $@

pn_04_mix_threads: $(PN_04_MIX_THREADS_BIN)

$(PN_04_MIX_THREADS_BIN): $(LIB) $(PN_MIX_SHARED) examples/pn/04-mix/main_threads.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) $(link_src) $(LIB) -o $@ -lpthread

pn_04_mix_coop: $(PN_04_MIX_COOP_BIN)

$(PN_04_MIX_COOP_BIN): $(LIB) $(PN_MIX_SHARED) examples/pn/04-mix/main_coop.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) $(link_src) $(LIB) -o $@

pn_04_mix_cyclic_trace: $(PN_04_MIX_CYCLIC_TRACE_BIN)

$(PN_04_MIX_CYCLIC_TRACE_BIN): $(TRACE_LIB) $(PN_MIX_SHARED) examples/pn/04-mix/main_cyclic.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) -DRX_TRACE_ENABLE $(trace_link_src) $(TRACE_LIB) -o $@

pn_04_mix_threads_trace: $(PN_04_MIX_THREADS_TRACE_BIN)

$(PN_04_MIX_THREADS_TRACE_BIN): $(TRACE_LIB) $(PN_MIX_SHARED) examples/pn/04-mix/main_threads.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) -DRX_TRACE_ENABLE $(trace_link_src) $(TRACE_LIB) -o $@ -lpthread

pn_04_mix_coop_trace: $(PN_04_MIX_COOP_TRACE_BIN)

$(PN_04_MIX_COOP_TRACE_BIN): $(TRACE_LIB) $(PN_MIX_SHARED) examples/pn/04-mix/main_coop.c | $(BUILD_DIR)
	$(CC) $(PN_MIX_FLAGS) -DRX_TRACE_ENABLE $(trace_link_src) $(TRACE_LIB) -o $@

# ------------------------------------------------------------------ #
# Mixed Examples                                                       #
# ------------------------------------------------------------------ #

mixed: $(MIXED_BIN)

MIXED_SRCS = \
    examples/fsm/01-light/app_driver.c \
    examples/fsm/01-light/cli_fsm.c \
    examples/fsm/01-light/light_fsm.c \
    examples/pn/01-light/light_pn.c \
    examples/mixed/main_cli.c

$(MIXED_BIN): $(LIB) $(MIXED_SRCS) | $(BUILD_DIR)
	$(CC) $(EXAMPLE_CFLAGS) -Iexamples/fsm/01-light -Iexamples/pn/01-light $(link_src) $(LIB) -o $@

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
	@echo "Running test_trace..."
	@$(TEST_TRACE_BIN)
	@echo ""
	@echo "All test suites passed."

$(TEST_RUNTIME_BIN): tests/test_runtime.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

$(TEST_FSM_BIN): tests/test_fsm.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

$(TEST_PN_BIN): tests/test_pn.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Itests $< $(LIB) -o $@

$(TEST_TRACE_BIN): tests/test_trace.c $(TRACE_SRCS) | $(BUILD_DIR)
	$(CC) $(TRACE_CFLAGS) -Itests $^ $(TRACE_LIB) -o $@ -lpthread

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
