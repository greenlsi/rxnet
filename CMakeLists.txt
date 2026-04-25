# ESP-IDF component registration for rxnet.
#
# Place rxnet as a component (managed or local):
#   - Managed: add to idf_component.yml dependencies and run `idf.py update-dependencies`
#   - Local:   set EXTRA_COMPONENT_DIRS pointing to the rxnet parent directory
#
# No REQUIRES needed: FreeRTOS and esp_timer are implicit in every ESP-IDF app.

idf_component_register(
    SRCS
        "c/src/runtime.c"
        "c/src/fsm.c"
        "c/src/pn.c"
        "c/src/cyclic.c"
        "c/src/coop.c"
        "c/src/thread.c"
    INCLUDE_DIRS "c/include"
)
