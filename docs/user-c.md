## 3. El tick y sus fases

Cada llamada a `rx_tick()` ejecuta cinco fases en orden estricto:

| # | Fase | Qué hace cada nodo |
|---|---|---|
| 1 | **Latch inputs** | `latch_inputs_cb(ctx, user)` — leer GPIO, calcular señales derivadas, tomar snapshot |
| 2 | **Evaluate** | `evaluate()` — decidir siguiente estado / flags (solo lectura del snapshot) |
| 3 | **Commit** | `commit()` — aplicar decisiones, encolar acciones diferidas |
| 4 | **Deferred** | ejecutar cola de acciones — timers, side-effects, notificaciones |
| 5 | **Dump outputs** | `dump_outputs_cb(ctx, user)` — escribir GPIO, actualizar salidas |

### Por qué esa separación de fases

La separación **evaluate / commit / deferred** no es arbitraria:

* **Evaluate** solo puede leer, nunca escribir estado observable. Todos los
  módulos ven el mismo estado del sistema al mismo tiempo.
* **Commit** aplica las decisiones. Tras commit, el estado es consistente pero
  los efectos externos todavía no han ocurrido.
* **Deferred** ejecuta las acciones (arrancar un timer, encender un LED) *después*
  de que todo el mundo haya aplicado sus decisiones. Una acción ve el estado
  comprometido de todos los módulos, no solo el propio.

La separación **latch / dump** garantiza que las lecturas de hardware ocurren
*antes* de evaluar (snapshot limpio) y las escrituras *después* de decidir
(salida consistente).

---

## 4. Máquinas de estado finito (FSM)

### 4.1 Cuándo usar una FSM

Una FSM es la herramienta adecuada cuando el comportamiento del módulo depende
de su **historia**: no solo del evento actual sino de lo que pasó antes.

Ejemplos típicos:

| Problema | Estados naturales |
|---|---|
| Luz con botón | OFF, ON |
| Semáforo | ROJO, VERDE, AMARILLO |
| Puerta con cerrojo | ABIERTA, CERRADA, BLOQUEADA |
| Protocolo de comunicación | IDLE, CONECTANDO, CONECTADO, ERROR |
| Menú de display | MENU_PRINCIPAL, SUBMENU_A, SUBMENU_B |

### 4.2 Anatomía de una máquina

Una `rx_fsm_machine` se define completamente en tiempo de compilación:
transiciones, estados y callbacks son punteros a funciones estáticas. No hay
allocación dinámica.

```c
#include "rxnet/fsm.h"

/* Estados: enteros arbitrarios */
enum { STATE_OFF = 0, STATE_ON = 1 };

/* Datos de usuario pasados a todos los callbacks */
typedef struct {
    bool latched_event;
    int  output_enabled;
} light_data_t;

/* Tabla de transiciones (puede ser static const) */
static const rx_fsm_transition transitions[] = {
    /* { from_state, to_state, guard, action } */
    { STATE_OFF, STATE_ON,  button_pressed, light_on  },
    { STATE_ON,  STATE_OFF, button_pressed, light_off },
};

static light_data_t data;
static rx_fsm_machine machine;

rx_fsm_machine_init(
    &machine,
    "light",                               /* nombre (debug) */
    STATE_OFF,                             /* estado inicial */
    transitions,
    sizeof(transitions) / sizeof(*transitions),
    &data,                                 /* user pointer */
    light_latch_cb,                        /* fase 1 */
    light_dump_cb                          /* fase 5 */
);
```

**Semántica first-match**: en cada tick, el runtime recorre las transiciones en
orden de declaración y ejecuta la *primera* cuya guardia devuelva distinto de
cero y cuyo estado origen coincida con el estado actual.

### 4.3 Guards y acciones

```c
/* Guard: pura, sin efectos secundarios; devuelve != 0 para "verdadero" */
static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const light_data_t *d = user;
    (void)ctx;
    return d->latched_event;
}

/* Acción: deferred, corre en fase 4 con estado ya comprometido */
static void light_on(rx_fsm_context *ctx, void *user) {
    light_data_t *d = user;
    (void)ctx;
    d->output_enabled = 1;
}

static void light_off(rx_fsm_context *ctx, void *user) {
    light_data_t *d = user;
    (void)ctx;
    d->output_enabled = 0;
}
```

**Regla importante**: los guards nunca deben tener efectos secundarios.
Son funciones puras de lectura. Los efectos van en las acciones.

### 4.4 Callbacks de fase

```c
/* Fase 1: tomar snapshot de entradas */
static void light_latch_cb(rx_fsm_context *ctx, void *user) {
    light_data_t *d = user;
    (void)ctx;
    d->latched_event = read_button_gpio();
}

/* Fase 5: escribir salidas */
static void light_dump_cb(rx_fsm_context *ctx, void *user) {
    const light_data_t *d = user;
    (void)ctx;
    set_led_gpio(d->output_enabled);
}
```

### 4.5 Ejemplo completo: luz con auto-apagado

```c
#include <stdint.h>
#include <stdbool.h>
#include "rxnet/fsm.h"

enum { STATE_OFF = 0, STATE_ON = 1 };

typedef struct {
    bool     latched_event;
    bool     enabled;
    uint32_t timeout_ms;
    uint32_t deadline_ms;
    uint32_t now_ms;
} auto_light_data_t;

static int pressed(const rx_fsm_context *ctx, void *user) {
    (void)ctx;
    return ((auto_light_data_t *)user)->latched_event;
}

static int timed_out(const rx_fsm_context *ctx, void *user) {
    const auto_light_data_t *d = user;
    (void)ctx;
    return d->now_ms >= d->deadline_ms;
}

static void turn_on(rx_fsm_context *ctx, void *user) {
    auto_light_data_t *d = user;
    (void)ctx;
    d->enabled = true;
    d->deadline_ms = d->now_ms + d->timeout_ms;
}

static void turn_off(rx_fsm_context *ctx, void *user) {
    auto_light_data_t *d = user;
    (void)ctx;
    d->enabled = false;
}

static void latch_cb(rx_fsm_context *ctx, void *user) {
    auto_light_data_t *d = user;
    (void)ctx;
    d->now_ms = get_time_ms();
    d->latched_event = read_button_gpio();
}

static void dump_cb(rx_fsm_context *ctx, void *user) {
    const auto_light_data_t *d = user;
    (void)ctx;
    set_led_gpio(d->enabled);
}

static const rx_fsm_transition transitions[] = {
    { STATE_OFF, STATE_ON,  pressed,   turn_on  },
    { STATE_ON,  STATE_ON,  pressed,   turn_on  },  /* reset timer */
    { STATE_ON,  STATE_OFF, timed_out, turn_off },
};

/* Montaje */
static auto_light_data_t data = { .timeout_ms = 5000 };
static rx_fsm_machine    machine;

void light_init(void) {
    rx_fsm_machine_init(
        &machine, "auto_light", STATE_OFF,
        transitions, sizeof(transitions) / sizeof(*transitions),
        &data, latch_cb, dump_cb
    );
}
```

### 4.6 El patrón de señales de entrada en FSM

La forma canónica de conectar hardware a una FSM en rxnet:

| Hardware | `latch_inputs_cb` | guards / actions |
|---|---|---|
| GPIO → evento | `data->latched = read_gpio()` | `if (data->latched) ...` |
| (vivo, volátil) | (snapshot estable) | (solo leen snapshot) |

El callback `latch_inputs_cb` es el único punto de contacto con el hardware.
El resto de la máquina trabaja con copias estables.

---

## 5. Redes de Petri (PN)

### 5.1 Cuándo usar una red de Petri

Una red de Petri es la herramienta adecuada cuando hay **concurrencia natural**:
varios procesos que avanzan en paralelo y necesitan sincronizarse.

Ejemplos típicos:

| Problema | Modela bien porque... |
|---|---|
| Botón toggle | Un token fluye entre P_OFF y P_ON |
| Buffer productor-consumidor | Tokens representan slots llenos/vacíos |
| Semáforo / mutex | Un token en P_LIBRE controla el acceso |
| Protocolo de handshake | Ambos lados avanzan y se sincronizan |
| Recursos compartidos | Tokens cuentan instancias disponibles |

La PN no reemplaza a la FSM: cuando el comportamiento es esencialmente lineal
(un agente con historia), la FSM es más clara. Cuando hay múltiples flujos que
se sincronizan, la PN es más clara.

### 5.2 Conceptos fundamentales

| Concepto | Símbolo | Significado |
|---|---|---|
| Lugar (place) | ○ | condición, estado, recurso, mensaje pendiente |
| Token | • | unidad de recurso, condición activa |
| Transición | rect. | evento, paso de proceso |
| Arco de entrada | →T | condición necesaria para disparar (consume tokens) |
| Arco de salida | T→ | condición que produce al disparar (produce tokens) |

Una transición **está habilitada** cuando todos sus lugares de entrada tienen
suficientes tokens (≥ peso del arco). Una transición habilitada **dispara**:
consume los tokens de sus entradas y produce tokens en sus salidas.

### 5.3 Semántica greedy-sequential

rxnet usa evaluación **greedy-sequential**: las transiciones se evalúan en
orden de declaración, y las anteriores *consumen tokens inmediatamente*, antes
de que las posteriores se evalúen.

Consecuencia práctica: **el orden de declaración implica prioridad**. Si dos
transiciones compiten por el mismo token, gana la que se declara primero.

```c
static const rx_pn_arc consume_x[] = {{ .place_id = P_X, .weight = 1 }};
static const rx_pn_arc produce_a[] = {{ .place_id = P_A, .weight = 1 }};
static const rx_pn_arc produce_b[] = {{ .place_id = P_B, .weight = 1 }};

static const rx_pn_transition transitions[] = {
    /* T0 declarada antes → prioridad sobre T1 */
    { consume_x, 1, produce_a, 1, NULL, NULL },
    /* T1: no dispara en el mismo tick si T0 consumió el token de P_X */
    { consume_x, 1, produce_b, 1, NULL, NULL },
};
```

### 5.4 Anatomía de una red

```c
#include "rxnet/pn.h"

enum { P_OFF = 0, P_ON = 1, P_REQUEST = 2 };

static const rx_pn_arc arcs_off_req[]  = {{ P_OFF, 1 }, { P_REQUEST, 1 }};
static const rx_pn_arc arcs_on[]       = {{ P_ON,  1 }};
static const rx_pn_arc arcs_on_req[]   = {{ P_ON,  1 }, { P_REQUEST, 1 }};
static const rx_pn_arc arcs_off[]      = {{ P_OFF, 1 }};

static const rx_pn_transition transitions[] = {
    /* off + request → on */
    { arcs_off_req, 2, arcs_on,  1, NULL, NULL },
    /* on  + request → off */
    { arcs_on_req,  2, arcs_off, 1, NULL, NULL },
};

static const int initial_places[] = { 1, 0, 0 };  /* token en P_OFF */
static rx_pn_net net;

rx_pn_net_init(
    &net,
    "light",
    initial_places, 3,
    transitions,    2,
    NULL,           /* user pointer */
    light_latch_cb, /* fase 1 — NULL → noop */
    light_dump_cb   /* fase 5 — NULL → noop */
);
```

Los arrays de arcos y transiciones pueden ser `static const`: el runtime no los
modifica, solo los lee. Los tokens (`net.places[]`) sí se modifican en cada tick.

### 5.5 Lugares de señal (signal places)

Un **lugar de señal** no acumula tokens: se *restablece* en cada latch a 0 o 1
según una condición del entorno.

```c
/* En latch_inputs_cb: reescribir P_TOGGLE_DUE en cada tick */
static void blink_latch_cb(rx_pn_context *ctx, void *user) {
    rx_pn_net *net = user;
    bool is_blinking = net->places[P_X1] > 0 || net->places[P_X2] > 0;
    net->places[P_TOGGLE_DUE] = (is_blinking && timer_elapsed()) ? 1 : 0;
    (void)ctx;
}
```

Contrasta con los **lugares de cola** (como P_REQUEST), que acumulan tokens:

```c
/* En latch_inputs_cb: añadir un token si hay pulsación */
static void light_latch_cb(rx_pn_context *ctx, void *user) {
    rx_pn_net *net = user;
    if (read_button_gpio())
        net->places[P_REQUEST]++;   /* acumula; se consume 1 por tick */
    (void)ctx;
}
```

| Tipo de lugar | Comportamiento | Ejemplo |
|---|---|---|
| **Señal** | Se reescribe a 0/1 cada latch | Timer expirado, sensor activo |
| **Cola** | Acumula; transición consume 1 | Pulsación de botón pendiente |
| **Estado** | Token único que fluye | ON/OFF, ROJO/VERDE |
| **Contador** | N tokens = N recursos | Slots libres en buffer |

### 5.6 Ejemplo completo: luz toggle

```c
#include <stdbool.h>
#include "rxnet/pn.h"

enum { P_OFF = 0, P_ON = 1, P_REQUEST = 2 };

typedef struct {
    bool output;
} light_pn_data_t;

static void action_turn_on(rx_pn_context *ctx, void *user) {
    ((light_pn_data_t *)user)->output = true;  (void)ctx;
}

static void action_turn_off(rx_pn_context *ctx, void *user) {
    ((light_pn_data_t *)user)->output = false;  (void)ctx;
}

static void latch_cb(rx_pn_context *ctx, void *user) {
    rx_pn_net *net = user;
    if (read_button_gpio())
        net->places[P_REQUEST]++;
    (void)ctx;
}

static void dump_cb(rx_pn_context *ctx, void *user) {
    set_led_gpio(((light_pn_data_t *)user)->output);  (void)ctx;
}

static const rx_pn_arc arcs_off_req[] = {{ P_OFF, 1 }, { P_REQUEST, 1 }};
static const rx_pn_arc arcs_on[]      = {{ P_ON,  1 }};
static const rx_pn_arc arcs_on_req[]  = {{ P_ON,  1 }, { P_REQUEST, 1 }};
static const rx_pn_arc arcs_off[]     = {{ P_OFF, 1 }};

static const rx_pn_transition transitions[] = {
    { arcs_off_req, 2, arcs_on,  1, NULL, action_turn_on  },
    { arcs_on_req,  2, arcs_off, 1, NULL, action_turn_off },
};

static const int      initial[]  = { 1, 0, 0 };
static light_pn_data_t pn_data;
static rx_pn_net       net;

void light_pn_init(void) {
    pn_data.output = false;
    rx_pn_net_init(
        &net, "light",
        initial, 3,
        transitions, 2,
        &net,      /* user = net para acceder a places[] en latch_cb */
        latch_cb, dump_cb
    );
}
```

---

## 6. Modelos de ejecución concurrente

Esta sección explica cómo rxnet se relaciona con los tres modelos clásicos de
ejecución en sistemas embebidos y de tiempo real.

### 6.1 Ejecutivo cíclico (Cyclic Executive)

#### Qué es

Un ejecutivo cíclico es el modelo más simple de concurrencia: un único hilo
de ejecución que repite la misma secuencia de tareas indefinidamente, con un
período fijo.

#### rxnet ES un ejecutivo cíclico

El bucle principal de cualquier ejemplo rxnet *es* un ejecutivo cíclico:

```c
#include <time.h>
#include "rxnet/runtime.h"

#define PERIOD_NS 10000000L  /* 10 ms */

void app_main(rx_runtime *rt) {
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    for (;;) {
        rx_tick(rt);

        next.tv_nsec += PERIOD_NS;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_sec++;
            next.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
}
```

Cada nodo registrado en el runtime es una "tarea" del ejecutivo. El orden de
registro determina el orden de ejecución dentro del frame:

```c
rx_runtime_add_node(&rt, &sensor_machine.node);   /* tarea 1 */
rx_runtime_add_node(&rt, &control_machine.node);  /* tarea 2: ve estado de tarea 1 */
rx_runtime_add_node(&rt, &actuator_net.node);     /* tarea 3: ve estado de las anteriores */
```

**Ventajas del ejecutivo cíclico**:

- Determinista: mismo orden en cada ciclo
- Sin overhead de scheduler
- Sin posibilidad de deadlock o starvation
- Fácil de analizar temporalmente: si cada tarea tarda ≤ T, el ciclo tarda ≤ N×T

**Limitaciones**:

- Todas las tareas comparten el mismo período
- Una tarea lenta retrasa todas las demás
- No hay forma de responder a eventos urgentes a mitad de ciclo

#### Multi-rate: múltiples runtimes a distintas velocidades

```c
rx_runtime fast_rt, slow_rt;
int tick_count = 0;

/* ... inicializar ambos runtimes ... */

for (;;) {
    rx_tick(&fast_rt);           /* siempre: 10 ms */
    if (tick_count % 10 == 0)
        rx_tick(&slow_rt);       /* cada 100 ms */
    tick_count++;
    sleep_10ms();
}
```

### 6.2 Multitarea cooperativa (Cooperative Multitasking)

En rxnet, cada nodo *cede implícitamente* al terminar su fase de evaluate.
El runtime es el scheduler; `rx_tick()` es la ronda de scheduling.

Una "tarea" puede necesitar **múltiples ticks** para completar su trabajo.
El estado se modela explícitamente como estado de FSM o como distribución de
tokens en la PN.

**Comunicación entre tareas cooperativas**:

1. **Lugares de PN (tokens como mensajes)** — el productor incrementa
   `net.places[P_BUFFER]` en su `latch_inputs_cb`; el consumidor dispara
   la transición que consume ese token.

2. **Estado de FSM como señal** — una máquina puede leer `machine_a.state`
   directamente en sus guards (el estado ya fue comprometido en la fase commit).

3. **Acciones diferidas con enqueue** — cualquier callback puede encolar una
   acción para la fase deferred del tick actual:

```c
rx_context_enqueue_deferred_action(ctx, my_action_fn, user_ptr);
```

| | Ejecutivo cíclico | Multitarea cooperativa |
|---|---|---|
| Unidad de trabajo | Todo en un tick | Puede abarcar N ticks |
| Estado entre ticks | No necesario | Capturado en estados/tokens |
| Modelo rxnet | Nodos sin estado persistente | FSM / PN con estado |

### 6.3 Threads con prioridades fijas (POSIX / RTOS)

#### rxnet dentro de una tarea RTOS (lo más común)

rxnet corre dentro de una única tarea RTOS. El RTOS gestiona las prioridades
reales; rxnet gestiona la lógica de la aplicación dentro de su tarea.

**FreeRTOS**:

```c
static void rxnet_task(void *arg) {
    rx_runtime *rt = arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        rx_tick(rt);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

/* Crear la tarea */
xTaskCreate(rxnet_task, "rxnet", 4096, &rt,
            tskIDLE_PRIORITY + 2, NULL);
```

**POSIX (Linux / macOS)**:

```c
#include <pthread.h>
#include <time.h>

static void *rxnet_thread(void *arg) {
    rx_runtime *rt = arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    for (;;) {
        rx_tick(rt);
        next.tv_nsec += 10000000L;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_sec++;
            next.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

pthread_t tid;
pthread_create(&tid, NULL, rxnet_thread, &rt);
```

#### Modelar prioridades dentro de rxnet (sin RTOS)

Los nodos registrados antes se evalúan primero. En PN con semántica
greedy-sequential, las transiciones declaradas antes tienen prioridad:

```c
static const rx_pn_arc consume_cpu[] = {{ P_CPU, 1 }};
static const rx_pn_arc produce_high[] = {{ P_TASK_HIGH, 1 }};
static const rx_pn_arc produce_low[]  = {{ P_TASK_LOW,  1 }};

static const rx_pn_transition transitions[] = {
    /* Alta prioridad: declarada primero */
    { consume_cpu, 1, produce_high, 1, high_prio_ready, NULL },
    /* Baja prioridad: solo dispara si la alta no consumió el token */
    { consume_cpu, 1, produce_low,  1, low_prio_ready,  NULL },
};
```

**Nota importante**: rxnet modela la **política** de scheduling; el SO/RTOS
implementa el **mecanismo** de preempción real.

### 6.4 Acciones diferidas asíncronas (worker pool)

Por defecto, la fase deferred ejecuta todas las acciones encoladas de forma
síncrona, en orden de prioridad, antes de continuar con dump. Para acciones
de larga duración que no deben bloquear el tick, rxnet expone un vtable
`rx_worker_pool` que el usuario implementa con el mecanismo de su plataforma.

#### Prioridades en la cola de deferred

Cualquier callback puede encolar una acción con prioridad explícita:

```c
/* Prioridad por defecto (NORMAL) */
rx_context_enqueue_deferred_action(ctx, send_data, user_ptr);

/* Prioridad explícita */
rx_context_enqueue_deferred_action_p(ctx, send_alarm,   user, RX_PRIORITY_CRITICAL);
rx_context_enqueue_deferred_action_p(ctx, log_event,    user, RX_PRIORITY_NORMAL);
rx_context_enqueue_deferred_action_p(ctx, update_stats, user, RX_PRIORITY_LOW);
```

| Nivel | Valor | Uso típico |
|---|---|---|
| `RX_PRIORITY_CRITICAL` | 3 | Alarmas, paradas de emergencia |
| `RX_PRIORITY_HIGH` | 2 | Control en tiempo real |
| `RX_PRIORITY_NORMAL` | 1 | Lógica de aplicación (defecto) |
| `RX_PRIORITY_LOW` | 0 | Telemetría, logging, estadísticas |

En modo síncrono (sin worker pool), las acciones se ordenan por prioridad
antes de ejecutarse (FIFO dentro del mismo nivel de prioridad).

#### Worker pool asíncrono (interfaz de vtable)

rxnet no incluye una implementación de worker pool: provee el vtable
`rx_worker_pool` que el usuario implementa con el mecanismo de su plataforma
(FreeRTOS queue, POSIX thread pool, etc.).

```c
#include "rxnet/runtime.h"

/* Implementación de ejemplo con POSIX (simplificada) */
typedef struct {
    rx_worker_pool base;   /* DEBE ser el primer campo */
    /* ... estado interno del pool ... */
} my_posix_pool;

static void my_pool_post(rx_worker_pool *self,
                          rx_deferred_action_fn fn,
                          rx_context *ctx,
                          void *user,
                          rx_priority_t priority)
{
    my_posix_pool *p = (my_posix_pool *)self;
    /* encolar {fn, ctx, user, priority} en la cola del pool */
    posix_pool_submit(p, fn, ctx, user, (int)priority);
}

static my_posix_pool g_pool = {
    .base = { .post = my_pool_post },
    /* ... */
};

/* Registrar el pool en el runtime */
rx_runtime_set_worker_pool(&rt, &g_pool.base);

/* A partir de aquí, rx_tick() publica las acciones al pool
 * y retorna inmediatamente sin esperar a que terminen. */
rx_tick(&rt);
```

Con un worker pool activo, `rx_tick()` **no bloquea** en la fase deferred:
llama a `pool->post()` para cada acción y pasa directamente a dump.
Los resultados llegan a las entradas del contexto en el siguiente latch.

```c
/* Quitar el pool (volver a modo síncrono) */
rx_runtime_set_worker_pool(&rt, NULL);
```

#### Nota sobre thread safety

`rx_tick()` no es thread-safe: solo un hilo puede llamarlo para un runtime
dado. La sincronización entre el hilo del tick y los workers del pool es
responsabilidad del usuario (p. ej. escribir resultados en `ctx` con una
variable atómica, o protegerlos con un mutex antes de leer en la fase latch).

### 6.5 Resumen comparativo

| Modelo | rxnet cómo lo implementa | Limitación |
|---|---|---|
| **Ejecutivo cíclico** | `rx_tick()` en bucle con `clock_nanosleep` | Todas las tareas al mismo período |
| **Multitarea cooperativa** | Estado en FSM/PN, tokens como mensajes | Sin preempción real |
| **Prioridad por policy** | Orden de nodos + greedy-sequential PN | Solo modela la política |
| **FreeRTOS task** | rxnet dentro de `xTaskCreate` | El RTOS gestiona la preempción real |
| **Multi-rate** | Múltiples runtimes a distintas frecuencias | Major/minor frames manuales |
| **Acciones asíncronas** | `rx_runtime_set_worker_pool()` + vtable | El usuario provee la implementación del pool |

---

## 7. Cuándo usar FSM, cuándo PN

### Usa FSM cuando...

* El módulo tiene **un agente con historia**: hay un único "estado del módulo"
  en cada momento.
* Las transiciones son mutuamente excluyentes: estar en un estado excluye estar
  en otro.
* El control de flujo es lineal (incluso si tiene bucles y ramas).
* Necesitas guards complejos que leen múltiples variables.

Ejemplos:

- Semáforo: ROJO → VERDE → AMARILLO → ROJO
- Cerrojo: DESBLOQUEADO → BLOQUEADO
- Protocolo: IDLE → SYN_SENT → ESTABLISHED

### Usa PN cuando...

* Hay **múltiples agentes independientes** que se sincronizan.
* Los recursos son contables (N slots, M conexiones disponibles).
* El paralelismo es natural: varias actividades ocurren al mismo tiempo.
* Necesitas modelar producción/consumo de forma explícita.

Ejemplos:

- Productor/consumidor: places `P_LLENOS` + `P_VACIOS`
- Semáforo binario: `P_MUTEX` (0 o 1 token)
- Rendezvous: A espera a B, B espera a A

### Mezclar ambos (patrón habitual)

Lo más potente es combinarlos en un único runtime base. La FSM describe la
**política** (qué modo estoy); la PN describe los **recursos y sincronización**
(qué tengo disponible):

```c
/* Sistema de acceso a sala:
 * FSM: gestiona el ciclo de apertura de puerta
 * PN:  gestiona los recursos (tokens = personas dentro) */

rx_context      ctx;
rx_runtime      rt;
rx_fsm_machine  door_machine;
rx_pn_net       capacity_net;

rx_context_init(&ctx);
rx_runtime_init(&rt, &ctx, 2);

door_fsm_init(&door_machine);
capacity_pn_init(&capacity_net);

rx_runtime_add_node(&rt, &door_machine.node);
rx_runtime_add_node(&rt, &capacity_net.node);

for (;;) {
    rx_tick(&rt);   /* FSM y PN avanzan juntos en el mismo tick */
    sleep_10ms();
}
```

Al compartir el mismo `rx_context`, las acciones diferidas de la FSM y de la
PN se ejecutan todas en la misma fase deferred del tick.

---

## 8. Referencia rápida de la API

### Core runtime

```c
#include "rxnet/runtime.h"

/* Contexto: cola de acciones diferidas */
rx_context ctx;
rx_context_init(&ctx);

/* Runtime: lista de nodos */
rx_runtime rt;
rx_runtime_init(&rt, &ctx, node_capacity);

/* Registrar nodos */
rx_runtime_add_node(&rt, &machine.node);   /* FSM o PN */

/* Ejecutar un ciclo */
rx_tick(&rt);

/* Encolar acción diferida con prioridad por defecto (NORMAL) */
rx_context_enqueue_deferred_action(&ctx, action_fn, user_ptr);

/* Encolar acción diferida con prioridad explícita */
rx_context_enqueue_deferred_action_p(&ctx, action_fn, user_ptr, RX_PRIORITY_HIGH);

/* Prioridades disponibles */
/* RX_PRIORITY_CRITICAL = 3   alarmas, paradas de emergencia */
/* RX_PRIORITY_HIGH     = 2   control en tiempo real          */
/* RX_PRIORITY_NORMAL   = 1   lógica de aplicación (defecto)  */
/* RX_PRIORITY_LOW      = 0   telemetría, logging             */

/* Worker pool asíncrono (implementación provista por el usuario) */
rx_runtime_set_worker_pool(&rt, &my_pool.base);  /* activar */
rx_runtime_set_worker_pool(&rt, NULL);            /* volver a modo síncrono */

/* Vtable que el usuario debe implementar */
struct rx_worker_pool {
    void (*post)(rx_worker_pool *self,
                 rx_deferred_action_fn fn,
                 rx_context *ctx,
                 void *user,
                 rx_priority_t priority);
};
```

### FSM

```c
#include "rxnet/fsm.h"

/* Tipos de función */
typedef int  (*rx_fsm_guard_fn)(const rx_fsm_context *ctx, void *user);
typedef void (*rx_fsm_action_fn)(rx_fsm_context *ctx, void *user);
typedef void (*rx_fsm_node_phase_fn)(rx_fsm_context *ctx, void *user);

/* Transición */
struct rx_fsm_transition {
    int                from_state;
    int                to_state;
    rx_fsm_guard_fn    guard;   /* NULL → siempre dispara */
    rx_fsm_action_fn   action;  /* NULL → sin efecto */
};

/* Inicializar máquina (stack allocation) */
void rx_fsm_machine_init(
    rx_fsm_machine          *machine,
    const char              *name,
    int                      initial_state,
    const rx_fsm_transition *transitions,
    size_t                   transition_count,
    void                    *user,
    rx_fsm_node_phase_fn     latch_inputs,   /* NULL → noop */
    rx_fsm_node_phase_fn     dump_outputs    /* NULL → noop */
);

/* Runtime dedicado FSM (contiene contexto propio) */
rx_fsm_runtime fsm_rt;
rx_fsm_runtime_init(&fsm_rt, machine_capacity);
rx_fsm_runtime_add_machine(&fsm_rt, &machine);
rx_fsm_tick(&fsm_rt);
```

### Petri Net

```c
#include "rxnet/pn.h"

/* Tipos de función */
typedef int  (*rx_pn_guard_fn)(const rx_pn_context *ctx, void *user);
typedef void (*rx_pn_action_fn)(rx_pn_context *ctx, void *user);
typedef void (*rx_pn_node_phase_fn)(rx_pn_context *ctx, void *user);

/* Arco */
struct rx_pn_arc {
    size_t place_id;
    int    weight;
};

/* Transición */
struct rx_pn_transition {
    const rx_pn_arc *consume;       /* array de arcos de entrada */
    size_t           consume_count;
    const rx_pn_arc *produce;       /* array de arcos de salida */
    size_t           produce_count;
    rx_pn_guard_fn   guard;         /* NULL → siempre dispara si habilitada */
    rx_pn_action_fn  action;        /* deferred, NULL → noop */
};

/* Inicializar red (stack allocation) */
int rx_pn_net_init(
    rx_pn_net               *net,
    const char              *name,
    const int               *initial_places,
    size_t                   place_count,
    const rx_pn_transition  *transitions,
    size_t                   transition_count,
    void                    *user,
    rx_pn_node_phase_fn      latch_inputs,   /* NULL → noop */
    rx_pn_node_phase_fn      dump_outputs    /* NULL → noop */
);

/* Runtime dedicado PN */
rx_pn_runtime pn_rt;
rx_pn_runtime_init(&pn_rt, net_capacity);
rx_pn_runtime_add_net(&pn_rt, &net);
rx_pn_tick(&pn_rt);
```

### Firmas de callbacks

```c
/* Guard: pura, sin efectos secundarios; devuelve != 0 para verdadero */
static int my_guard(const rx_fsm_context *ctx, void *user) {
    (void)ctx;
    return ((my_data_t *)user)->latched_event;
}

/* Acción: deferred, corre en fase 4 con estado ya comprometido */
static void my_action(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    ((my_data_t *)user)->output = 1;
}

/* Latch: snapshot hardware, señales derivadas */
static void latch_cb(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    my_data_t *d = user;
    d->latched_event = read_gpio(d->button_pin);
}

/* Dump: escribir salidas al hardware */
static void dump_cb(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    const my_data_t *d = user;
    write_gpio(d->led_pin, d->output);
}
```

---

## Lectura adicional

- **Código fuente**: `c/` — runtime (~200 líneas), fsm (~150 líneas), pn (~200 líneas)
- **Tests**: `c/tests/` — test_fsm.c, test_pn.c, parity_runner.c
- **Ejemplos**: `c/examples/fsm/` y `c/examples/pn/` — ejecutables para macOS/Linux
- **Especificaciones**: `docs/specs/c/requirements.md` y `docs/specs/c/design.md`
- **Implementación Python**: `python/` — misma semántica, API ergonómica en Python 3.14+
