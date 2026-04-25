# Zephyr module integration for rxnet.
#
# Discovered automatically when rxnet is registered as a module via
# ZEPHYR_EXTRA_MODULES or a west manifest.  The app does not need to
# reference rxnet explicitly — Zephyr links it and exposes the headers.

zephyr_library_named(rxnet)

zephyr_library_sources(
    ../c/src/runtime.c
    ../c/src/fsm.c
    ../c/src/pn.c
    ../c/src/cyclic.c
    ../c/src/coop.c
    ../c/src/thread.c
    ../c/src/port/zephyr.c
)

# Headers available to the library itself and to all consumers.
zephyr_include_directories(../c/include)
