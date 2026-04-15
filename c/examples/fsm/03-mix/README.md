# FSM 03-mix (ESP-IDF + CLI host)

Mixed example with one machine of each type:

- A: `00-light` behavior (toggle ON/OFF per button A press)
- B: `02-blink` behavior (OFF -> X1 -> X2 -> OFF per button A press)
- C: `01-auto` behavior (ON with button B, auto-OFF after timeout)

## Files

- `main.c`: ESP-IDF periodic loop
- `main_cli.c`: host/macOS runtime loop with mixed commands
- `cli_fsm.c/.h`: reusable CLI FSM
- `light_fsm.c/.h`: machine A (light)
- `blink_fsm.c/.h`: machine B (blink)
- `auto_fsm.c/.h`: machine C (auto-off)
- `app_driver.c/.h`: ESP-IDF GPIO + ISR

## Host/macOS CLI build

```bash
make -C c mix_cli
./c/build/fsm_03_mix
```

CLI commands:

- `a` or `press a`: trigger shared button A (affects A and B)
- `b` or `press b`: trigger button B (affects C)
- `status`: print state/output of A, B and C
- `freq <hz>`: set base blink frequency for B
- `timeout <ms>`: set auto-off timeout for C
- `quit`: exit
