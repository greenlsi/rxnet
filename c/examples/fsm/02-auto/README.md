# FSM 01-auto (ESP-IDF + CLI host)

This example is similar to `00-light`, but each light has auto-off by inactivity:

- button press turns the light on
- if the light is already on, button press refreshes its inactivity timer
- if no new press arrives before the configured timeout, the light turns off automatically
- each machine has its own timeout

## Files

- `main.c`: ESP-IDF periodic loop
- `main_cli.c`: host/macOS runtime loop with timeout commands
- `cli_fsm.c/.h`: reusable CLI FSM (raw mode, cmdline handling, command registry)
- `auto_fsm.c/.h`: auto FSM with auto-off timeout per machine
- `app_driver.c/.h`: ESP-IDF GPIO + ISR

## Host/macOS CLI build

```bash
make -C c auto_cli
./c/build/fsm_01_auto
```

CLI commands:

- `a` or `press a`: trigger shared button A (affects lights A and B)
- `b` or `press b`: trigger button B (affects light C)
- `status`: print machine/output status and current auto-off timeouts
- `timeout <a|b|c> <ms>`: change auto-off timeout for one light
- `sched`: report schedulability analysis using measured WCET samples
- `quit`: exit
