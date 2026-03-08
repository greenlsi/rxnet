# FSM 00-light (ESP-IDF)

This example models a light controller with one push button.

- short press toggles light OFF/ON
- FSM logic is isolated in `light_fsm.c`
- hardware details are isolated in `app_driver.c`
- ISR writes directly to `runtime.context.inputs` (`button_press_event = true`)
- loop runs periodically with `vTaskDelayUntil(...)`, calls `rx_fsm_tick()`, then clears input flag

## Files

- `main.c`: periodic loop and runtime wiring
- `light_fsm.c/.h`: FSM constructor, guards, actions
- `app_driver.c/.h`: GPIO setup, ISR, light output

## Wiring

- `BUTTON_GPIO` (`GPIO_NUM_0`) to push button, active low with pull-up
- `LIGHT_GPIO` (`GPIO_NUM_2`) to LED (or transistor input)

## Tick model in this example

1. ISR stores button event in `inputs`
2. loop runs at fixed period (`vTaskDelayUntil`)
3. loop calls `rx_fsm_tick()` (guards read `latched_inputs`)
4. loop clears the event in live `inputs`
