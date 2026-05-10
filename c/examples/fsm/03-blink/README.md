# FSM 02-blink (ESP-IDF + CLI host)

This example cycles each light through 3 modes on each button press:

- first press: blink at base frequency (x1)
- second press: blink at double frequency (x2)
- third press: turn OFF

## Files

- `main.c`: ESP-IDF periodic loop
- `main_cli.c`: host/macOS runtime loop with frequency commands
- `cli_fsm.c/.h`: reusable CLI FSM (raw mode, cmdline handling, command registry)
- `blink_fsm.c/.h`: blink FSM with 3-state press cycle and configurable base frequency
- `app_driver.c/.h`: ESP-IDF GPIO + ISR

## Host/macOS CLI build

```bash
make -C c blink_cli
./c/build/fsm_02_blink
```

CLI commands:

- `a` or `press a`: trigger shared button A (affects lights A and B)
- `b` or `press b`: trigger button B (affects light C)
- `status`: print states, outputs, base and effective frequencies
- `freq <a|b|c> <hz>`: set base frequency for one light
- `sched`: report schedulability analysis using measured WCET samples
- `quit`: exit
