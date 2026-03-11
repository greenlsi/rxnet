# FSM 00-light (ESP-IDF + CLI host)

This example models three light state machines:

- light A and light B share button A
- light C uses button B independently
- each light is a separate FSM machine
- one shared app input struct (`light_inputs`) is latched globally per tick
- each machine projects shared app input into machine-local input before guard evaluation

## Files

- `main.c`: ESP-IDF periodic loop
- `main_cli.c`: host/macOS CLI loop
- `light_fsm.c/.h`: FSM constructor, input projection, guards, actions
- `app_driver.c/.h`: ESP-IDF GPIO + ISR

## ESP-IDF wiring

- `BUTTON_A_GPIO` (`GPIO_NUM_0`) shared by light A and light B
- `BUTTON_B_GPIO` (`GPIO_NUM_15`) used by light C
- `LIGHT_A_GPIO` (`GPIO_NUM_2`) to first LED
- `LIGHT_B_GPIO` (`GPIO_NUM_4`) to second LED
- `LIGHT_C_GPIO` (`GPIO_NUM_5`) to third LED

## Host/macOS CLI build

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic \
  -I../../../include \
  ../../../src/runtime.c ../../../src/fsm.c \
  light_fsm.c main_cli.c -o light_cli
./light_cli
```

CLI commands:

- `a` or `press a`: trigger shared button A (affects lights A and B)
- `b` or `press b`: trigger button B (affects light C)
- `tick`: run one periodic tick with no button press
- `status`: print machine/output status
- `quit`: exit
