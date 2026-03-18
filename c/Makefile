CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Iinclude
EXAMPLE_CFLAGS ?= $(CFLAGS) -Wno-unused-parameter

.PHONY: all clean

all: fsm_example pn_example light_cli

fsm_example: src/runtime.c src/fsm.c examples/example.c
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

pn_example: src/runtime.c src/pn.c examples/pn_example.c
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

light_cli: src/runtime.c src/fsm.c examples/fsm/00-light/app_driver.c examples/fsm/00-light/light_fsm.c examples/fsm/00-light/cli_fsm.c examples/fsm/00-light/main_cli.c
	$(CC) $(EXAMPLE_CFLAGS) $^ -o $@

clean:
	rm -f fsm_example pn_example light_cli
