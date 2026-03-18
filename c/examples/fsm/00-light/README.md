# FSM 00-light (ESP-IDF + CLI host)

This example models three light state machines:

- light A and light B share button A
- light C uses button B independently
- each light is a separate FSM machine
- each machine receives a pointer to its button event flag
- each machine latches its own event and clears that flag in `dump_outputs`

## Files

- `main.c`: ESP-IDF periodic loop
- `main_cli.c`: host/macOS runtime loop (registers FSMs and ticks periodically)
- `cli_fsm.c/.h`: reusable CLI FSM (raw mode, cmdline handling, command registry, per-command user_data)
- `light_fsm.c/.h`: reusable FSM constructor with private machine data and node-phase callbacks
- `app_driver.c/.h`: ESP-IDF GPIO + ISR

## ESP-IDF wiring

- `BUTTON_A_GPIO` (`GPIO_NUM_0`) shared by light A and light B
- `BUTTON_B_GPIO` (`GPIO_NUM_15`) used by light C
- `LIGHT_A_GPIO` (`GPIO_NUM_2`) to first LED
- `LIGHT_B_GPIO` (`GPIO_NUM_4`) to second LED
- `LIGHT_C_GPIO` (`GPIO_NUM_5`) to third LED

## Host/macOS CLI build

```bash
make -C c light_cli
./c/light_cli
```

CLI commands:

- `a` or `press a`: trigger shared button A (affects lights A and B)
- `b` or `press b`: trigger button B (affects light C)
- `status`: print machine/output status
- `quit`: exit
- `tick`: no-op (ticks are already periodic)

`main_cli.c` registers these commands with `cli_fsm_register_command(...)`.
