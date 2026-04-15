# rxnet — Guía para alumnos

## Índice

1. [El problema que resuelve rxnet](#1-el-problema-que-resuelve-rxnet)
2. [La hipótesis síncrona](#2-la-hipótesis-síncrona)
3. [El tick y sus fases](#3-el-tick-y-sus-fases)
4. [Máquinas de estado finito (FSM)](#4-máquinas-de-estado-finito-fsm)
5. [Redes de Petri (PN)](#5-redes-de-petri-pn)
6. [Modelos de ejecución concurrente](#6-modelos-de-ejecución-concurrente)
7. [Cuándo usar FSM, cuándo PN](#7-cuándo-usar-fsm-cuándo-pn)
8. [Referencia rápida de la API](#8-referencia-rápida-de-la-api)

---

## 1. El problema que resuelve rxnet

Los sistemas reactivos responden a eventos del entorno: pulsaciones de botón,
lecturas de sensor, mensajes de red, expiración de temporizadores. El reto no
es *qué hacer* ante un evento, sino *cuándo hacerlo* y *qué hay que ver* en ese
momento.

Sin disciplina, el código reactivo acumula dos tipos de bugs difíciles de
reproducir:

* **Race conditions de lectura**: el guard de una transición lee un bit de
  hardware a mitad de ciclo, cuando otro módulo ya lo consumió.
* **Efectos secundarios prematuros**: una acción ejecutada durante la evaluación
  modifica estado que otro módulo todavía no evaluó.

rxnet impone una disciplina que los elimina por construcción: **toda la
información que entra en el sistema se lee exactamente una vez por ciclo, al
principio, y todos los efectos secundarios se ejecutan exactamente una vez,
al final**.

---

## 2. La hipótesis síncrona

rxnet está inspirada en los *lenguajes síncronos* (Esterel, Lustre, Signal) y
en sus derivados industriales (SCADE, Simulink). La idea central se llama la
**hipótesis síncrona**:

> La computación de un tick es instantánea. El mundo externo no cambia mientras
> el sistema está procesando.

Esto es una abstracción, no una afirmación física. En la práctica significa:

* El tick debe completarse antes de que llegue el siguiente evento significativo.
* Si el tick tarda más que el período de muestreo, el sistema tiene un problema
  de diseño, no de concurrencia.

Bajo la hipótesis síncrona, **no hay carrera de datos posible dentro del tick**:
todos los módulos leen el mismo snapshot de entradas, y nadie ve los cambios de
los demás hasta el siguiente tick.

---

## 3. El tick y sus fases

Cada llamada a `runtime.tick()` ejecuta seis fases en orden estricto:

```
┌─────────────────────────────────────────────────────────────────┐
│  TICK                                                           │
│                                                                 │
│  1. Global latch  ─── context.latched_inputs = copy(inputs)    │
│                                                                 │
│  2. Per-node latch ── para cada nodo: latch_inputs_cb()        │
│                        (leer GPIO, calcular señales derivadas)  │
│                                                                 │
│  3. Evaluate ──────── para cada nodo: evaluate()               │
│                        (decidir siguiente estado / flags)       │
│                                                                 │
│  4. Commit ────────── para cada nodo: commit()                 │
│                        (aplicar decisiones, encolar acciones)  │
│                                                                 │
│  5. Deferred ──────── ejecutar cola de acciones                │
│                        (timers, side-effects, notificaciones)  │
│                                                                 │
│  6. Dump outputs ──── para cada nodo: dump_outputs_cb()        │
│                        (escribir GPIO, imprimir estado)         │
└─────────────────────────────────────────────────────────────────┘
```

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

```python
from rxnet.fsm import Machine, Transition, Runtime

# Estados: constantes enteras (el runtime no sabe qué significan)
IDLE    = 0
RUNNING = 1
ERROR   = 2

# Transiciones: (estado_origen, estado_destino, guardia?, acción?)
transitions = [
    Transition(IDLE,    RUNNING, guard=button_pressed, action=start_motor),
    Transition(RUNNING, IDLE,    guard=button_pressed, action=stop_motor),
    Transition(RUNNING, ERROR,   guard=fault_detected, action=report_fault),
    Transition(ERROR,   IDLE,    guard=reset_pressed,  action=clear_fault),
]

machine = Machine(
    name="motor",
    state=IDLE,
    transitions=transitions,
    user=my_data,              # datos de usuario, pasados a guard/action
    latch_inputs_cb=read_gpio, # llamada en fase 2
    dump_outputs_cb=write_gpio,# llamada en fase 6
)

rt = Runtime()
rt.add_machine(machine)
```

**Semántica first-match**: en cada tick, el runtime recorre las transiciones en
orden de declaración y ejecuta la *primera* cuya guardia sea verdadera y cuyo
estado origen coincida con el estado actual. Si ninguna coincide, la máquina
permanece en su estado.

### 4.3 Guards y acciones

```python
from rxnet.runtime import Context

# Guard: función pura que devuelve bool
# Recibe el contexto del tick y los datos de usuario
def button_pressed(ctx: Context, data: MyData) -> bool:
    return data.latched_button_event  # leer snapshot, nunca hardware en vivo

# Acción: función deferred (corre en fase 5, después de commit)
# En este punto todos los módulos ya aplicaron sus cambios
def start_motor(ctx: Context, data: MyData) -> None:
    data.motor_enabled = True
    # Aquí podemos consultar otros módulos: su estado ya está comprometido
```

**Regla importante**: los guards nunca deben tener efectos secundarios.
Son funciones puras de lectura. Los efectos van en las acciones.

### 4.4 Ejemplo completo: luz con auto-apagado

```python
import time
from rxnet.fsm import Machine, Transition, Runtime
from rxnet.runtime import Context

# -- Estados --
OFF = 0
ON  = 1

# -- Datos de la máquina --
class LightData:
    def __init__(self, timeout_ms: int):
        self.latched_event = False
        self.enabled = False
        self.timeout_ms = timeout_ms
        self.deadline_ms = 0
        self.now_ms = 0

def now_ms() -> int:
    return int(time.monotonic() * 1000)

# -- Callbacks de fase --
def latch(ctx: Context, d: LightData) -> None:
    d.now_ms = now_ms()
    d.latched_event = read_button()   # snapshot del hardware

def dump(ctx: Context, d: LightData) -> None:
    set_led(d.enabled)

# -- Guards --
def pressed(ctx: Context, d: LightData) -> bool:
    return d.latched_event

def timed_out(ctx: Context, d: LightData) -> bool:
    return d.now_ms >= d.deadline_ms

# -- Acciones (deferred: ven el estado comprometido) --
def turn_on(ctx: Context, d: LightData) -> None:
    d.enabled = True
    d.deadline_ms = now_ms() + d.timeout_ms

def turn_off(ctx: Context, d: LightData) -> None:
    d.enabled = False

# -- Montaje --
data = LightData(timeout_ms=5000)
machine = Machine(
    name="light",
    state=OFF,
    transitions=[
        Transition(OFF, ON,  guard=pressed,   action=turn_on),
        Transition(ON,  ON,  guard=pressed,   action=turn_on),  # reset timer
        Transition(ON,  OFF, guard=timed_out, action=turn_off),
    ],
    user=data,
    latch_inputs_cb=latch,
    dump_outputs_cb=dump,
)

rt = Runtime()
rt.add_machine(machine)

# Bucle principal (10 ms)
while True:
    rt.tick()
    time.sleep(0.010)
```

### 4.5 El patrón de señales de entrada en FSM

La forma canónica de conectar hardware a una FSM en rxnet:

```
Hardware            latch_inputs_cb          guards/actions
───────────         ────────────────         ──────────────
GPIO → evento   →   data.latched = True   →  if data.latched: ...
(vivo, volátil)     (snapshot estable)        (solo leen snapshot)
```

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

```
Lugar (place):   círculo ○   ←→   condición, estado, recurso, mensaje pendiente
Token:           punto •     ←→   unidad de recurso, condición activa
Transición:      rectángulo  ←→   evento, paso de proceso
Arco de entrada: flecha →T   ←→   condición necesaria para disparar (consume)
Arco de salida:  flecha T→   ←→   condición que produce al disparar (produce)
```

Una transición **está habilitada** cuando todos sus lugares de entrada tienen
suficientes tokens (≥ peso del arco). Una transición habilitada **dispara**:
consume los tokens de sus entradas y produce tokens en sus salidas.

### 5.3 Semántica greedy-sequential

rxnet usa evaluación **greedy-sequential**: las transiciones se evalúan en
orden de declaración, y las anteriores *consumen tokens inmediatamente*, antes
de que las posteriores se evalúen.

Consecuencia práctica: **el orden de declaración implica prioridad**. Si dos
transiciones compiten por el mismo token, gana la que se declara primero.

```python
transitions = [
    # T0 declarada antes → prioridad sobre T1
    Transition(consume=[Arc(P_X, 1)], produce=[Arc(P_A, 1)]),  # T0
    Transition(consume=[Arc(P_X, 1)], produce=[Arc(P_B, 1)]),  # T1 (no dispara si T0 disparó)
]
```

### 5.4 Anatomía de una red

```python
from rxnet.pn import Net, Transition, Arc, Runtime

# Lugares: índices 0, 1, 2, ...
P_OFF     = 0
P_ON      = 1
P_REQUEST = 2

net = Net(
    name="light",
    places=[1, 0, 0],   # token inicial en P_OFF
    transitions=[
        # off + request → on
        Transition(consume=[Arc(P_OFF, 1), Arc(P_REQUEST, 1)],
                   produce=[Arc(P_ON, 1)]),
        # on + request → off
        Transition(consume=[Arc(P_ON, 1), Arc(P_REQUEST, 1)],
                   produce=[Arc(P_OFF, 1)]),
    ],
)

rt = Runtime()
rt.add_net(net)
```

### 5.5 Lugares de señal (signal places)

Un **lugar de señal** no acumula tokens: se *restablece* en cada latch a 0 o 1
según una condición del entorno. Es el equivalente PN de los inputs latched de
una FSM.

```python
# En latch_inputs_cb: reescribir P_TOGGLE_DUE en cada tick
def latch_cb(ctx, _user):
    is_blinking = net.places[P_X1] > 0 or net.places[P_X2] > 0
    net.places[P_TOGGLE_DUE] = 1 if (is_blinking and timer_elapsed()) else 0
```

Contrasta con los **lugares de cola** (como P_REQUEST), que acumulan tokens:

```python
# En latch_inputs_cb: añadir un token si hay pulsación
def latch_cb(ctx, _user):
    if button_pressed():
        net.places[P_REQUEST] += 1   # acumula; se consume 1 por tick
```

| Tipo de lugar | Comportamiento | Ejemplo |
|---|---|---|
| **Señal** | Se reescribe a 0/1 cada latch | Timer expirado, sensor activo |
| **Cola** | Acumula; transición consume 1 | Pulsación de botón pendiente |
| **Estado** | Token único que fluye | ON/OFF, ROJO/VERDE |
| **Contador** | N tokens = N recursos | Slots libres en buffer |

### 5.6 Ejemplo completo: blink con tres velocidades

```
OFF ──[botón]──► X1 ──[botón]──► X2 ──[botón]──► OFF
X1 ──[toggle]──► X1   (self-loop: invertir LED)
X2 ──[toggle]──► X2   (self-loop: invertir LED a 2× velocidad)
```

```python
P_OFF, P_X1, P_X2, P_REQUEST, P_TOGGLE_DUE = 0, 1, 2, 3, 4

def create_blink_pn(button_gpio, light_gpio, base_hz):
    state = {"output": False, "next_toggle_ms": 0, "now_ms": 0}

    def action_enter_x1(ctx, _): state["output"] = True; ...
    def action_enter_x2(ctx, _): state["output"] = True; ...
    def action_enter_off(ctx, _): state["output"] = False; state["next_toggle_ms"] = 0
    def action_toggle(ctx, _): state["output"] = not state["output"]; ...

    net = Net(
        name="blink",
        places=[1, 0, 0, 0, 0],
        transitions=[
            # Transiciones de botón ANTES que las de toggle (prioridad greedy)
            Transition(consume=[Arc(P_OFF,1), Arc(P_REQUEST,1)], produce=[Arc(P_X1,1)], action=action_enter_x1),
            Transition(consume=[Arc(P_X1,1), Arc(P_REQUEST,1)], produce=[Arc(P_X2,1)], action=action_enter_x2),
            Transition(consume=[Arc(P_X2,1), Arc(P_REQUEST,1)], produce=[Arc(P_OFF,1)], action=action_enter_off),
            # Self-loops de toggle DESPUÉS (pierden si hay botón en el mismo tick)
            Transition(consume=[Arc(P_X1,1), Arc(P_TOGGLE_DUE,1)], produce=[Arc(P_X1,1)], action=action_toggle),
            Transition(consume=[Arc(P_X2,1), Arc(P_TOGGLE_DUE,1)], produce=[Arc(P_X2,1)], action=action_toggle),
        ],
    )
    # ... latch/dump callbacks
    return net
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

```
t=0ms   t=10ms  t=20ms  t=30ms
  │       │       │       │
  ▼       ▼       ▼       ▼
[A][B][C][A][B][C][A][B][C]...

A = tarea de sensores
B = tarea de control
C = tarea de actuadores
```

#### rxnet ES un ejecutivo cíclico

El bucle principal de cualquier ejemplo rxnet *es* un ejecutivo cíclico:

```python
# Esto es literalmente un ejecutivo cíclico
PERIOD_S = 0.010   # 10 ms

next_tick = time.monotonic()
while True:
    rt.tick()                          # ← el "frame" del ejecutivo

    next_tick += PERIOD_S
    sleep_s = next_tick - time.monotonic()
    if sleep_s > 0:
        time.sleep(sleep_s)
```

Cada nodo registrado en el runtime es una "tarea" del ejecutivo. El orden de
registro determina el orden de ejecución dentro del frame:

```python
rt.add_machine(sensor_machine)    # tarea 1: se evalúa primero
rt.add_machine(control_machine)   # tarea 2: ve el estado comprometido de tarea 1
rt.add_machine(actuator_machine)  # tarea 3: ve el estado de las anteriores
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

#### Ajuste fino: múltiples runtimes a distintas velocidades

Para tareas con períodos diferentes, se pueden usar múltiples runtimes con
bucles independientes, o ejecutar algunos nodos con división de frecuencia:

```python
tick_count = 0
while True:
    fast_runtime.tick()          # siempre: 10 ms
    if tick_count % 10 == 0:
        slow_runtime.tick()      # cada 100 ms
    tick_count += 1
    time.sleep(0.010)
```

### 6.2 Multitarea cooperativa (Cooperative Multitasking)

#### Qué es

En la multitarea cooperativa, cada tarea cede el control voluntariamente cuando
termina su trabajo. No hay interrupciones; el scheduler solo actúa en los puntos
de *yield*. Python `asyncio` y MicroPython son ejemplos modernos.

```
Tarea A:  ████████░░░░░░░░████████░░░░░░
Tarea B:  ░░░░░░░░████████░░░░░░░░████████
                  ↑        ↑
               yield A   yield B
```

#### rxnet como multitarea cooperativa

En rxnet, cada nodo *cede implícitamente* al terminar su `evaluate()`. El
runtime es el scheduler; `tick()` es la ronda de scheduling.

La diferencia conceptual respecto al ejecutivo cíclico es que una "tarea" puede
necesitar **múltiples ticks** para completar su trabajo. El estado de la tarea
(en qué punto del trabajo está) se modela explícitamente como estado de FSM o
como distribución de tokens en la PN.

**Ejemplo: tarea que procesa una cola de mensajes, uno por tick**

```
Modelo FSM:

IDLE ──[hay_mensaje]──► PROCESANDO ──[procesado]──► IDLE
                              │
                              └──[error]──► ERROR_HANDLER
```

```python
IDLE         = 0
PROCESANDO   = 1
ERROR_HANDLER = 2

class WorkerData:
    def __init__(self, queue):
        self.queue = queue
        self.current_msg = None

def latch(ctx, d: WorkerData):
    # Tomamos UN mensaje por tick (no vaciamos toda la cola)
    d.current_msg = d.queue.pop(0) if d.queue else None

def has_message(ctx, d): return d.current_msg is not None
def is_processed(ctx, d): return True  # en este tick simple, siempre termina
def has_error(ctx, d): return d.current_msg and d.current_msg.is_invalid()

def process(ctx, d):
    do_work(d.current_msg)  # acción deferred: todos los módulos ya commitaron

machine = Machine(
    name="worker",
    state=IDLE,
    transitions=[
        Transition(IDLE,       PROCESANDO,   guard=has_message),
        Transition(PROCESANDO, IDLE,          guard=is_processed, action=process),
        Transition(PROCESANDO, ERROR_HANDLER, guard=has_error),
    ],
    user=WorkerData(my_queue),
    latch_inputs_cb=latch,
)
```

#### Comunicación entre tareas cooperativas

Las tareas se comunican a través de:

1. **Lugares de PN (tokens como mensajes)**:
```python
# El productor añade un token en P_BUFFER cuando tiene datos
# El consumidor consume ese token cuando está listo
P_BUFFER = 2
net.places[P_BUFFER] += 1  # en latch_inputs_cb del productor
```

2. **Estado de FSM como señal de coordinación**:
```python
# Tarea B puede leer el estado de tarea A en sus guards
def a_is_ready(ctx, d):
    return task_a_machine.state == READY  # acceso directo al estado
```

3. **Context inputs (datos globales del tick)**:
```python
# En el context global se puede poner un struct de señales
# compartidas que todas las máquinas leen durante latch
ctx.latched_inputs.alarm_active = sensor_data.alarm
```

#### Diferencia clave con el ejecutivo cíclico

| | Ejecutivo cíclico | Multitarea cooperativa |
|---|---|---|
| Unidad de trabajo | Todo en un tick | Puede abarcar N ticks |
| Estado entre ticks | No necesario | Capturado en estados/tokens |
| Comunicación | Secuencia fija | Señales explícitas |
| Modelo rxnet | Nodos sin estado persistente | FSM / PN con estado |

### 6.3 Threads con prioridades fijas y desalojo

#### Qué es

En un RTOS (FreeRTOS, Zephyr, ThreadX), las tareas tienen prioridades
numéricas. El scheduler **desaloja** (preempt) una tarea de baja prioridad
cuando una de alta prioridad pasa a estar lista para ejecutar, incluso si la
primera no ha terminado su trabajo.

```
alta prioridad:  ░░░░████░░░░████████░░░░
baja prioridad:  ████░░░░████░░░░░░░░████
                     ↑            ↑
                  preempción    preempción
```

#### ¿Puede rxnet modelar esto?

Directamente, **no**: rxnet es síncrono y cooperativo. Un tick es atómico;
nada puede interrumpirlo a mitad.

Pero rxnet puede usarse de dos formas en un sistema con hilos preemptivos:

---

**Forma 1: rxnet dentro de una tarea RTOS (lo más común)**

rxnet corre dentro de una única tarea RTOS. El RTOS gestiona las prioridades
reales entre tareas del sistema; rxnet gestiona la lógica de la aplicación
dentro de su tarea.

```
RTOS scheduler
  │
  ├── Tarea 1 (alta prioridad): IRQ de emergencia
  ├── Tarea 2 (media prioridad): ← rxnet corre aquí
  │     └── rt.tick() cada 10 ms
  │           ├── sensor_machine
  │           ├── control_machine
  │           └── actuator_machine
  └── Tarea 3 (baja prioridad): logging, comms
```

El RTOS garantiza que la tarea rxnet se ejecuta con su prioridad. Dentro de
esa tarea, el ejecutivo cíclico de rxnet gestiona las sub-tareas sin overhead
de RTOS.

---

**Forma 2: modelar prioridades dentro de rxnet**

Si *todas* las "tareas" son manejadas por rxnet (sin RTOS), se puede modelar
comportamiento de prioridades usando las propiedades del modelo:

**a) Prioridad por orden de nodo**

Los nodos registrados antes se evalúan primero. En PN con semántica
greedy-sequential, las transiciones declaradas antes tienen prioridad sobre
las declaradas después para el mismo recurso:

```python
transitions = [
    # Prioridad alta: esta transición se evalúa primero
    # Si dispara, consume el token y la de baja prioridad no puede
    Transition(consume=[Arc(P_CPU, 1)], produce=[Arc(P_TASK_HIGH, 1)],
               guard=high_prio_ready),     # ← alta prioridad

    # Prioridad baja: solo dispara si la alta no consumió el token
    Transition(consume=[Arc(P_CPU, 1)], produce=[Arc(P_TASK_LOW, 1)],
               guard=low_prio_ready),      # ← baja prioridad
]
```

**b) Modelar desalojo con señales**

El "desalojo" se puede modelar como una transición que devuelve el token de
CPU al pool cuando llega una tarea de mayor prioridad:

```
Estado normal:
  P_CPU=1, P_LOW_RUNNING=1

Llega alta prioridad:
  T_PREEMPT: {P_LOW_RUNNING, P_HIGH_READY} → {P_HIGH_RUNNING, P_LOW_SUSPENDED}

Alta prioridad termina:
  T_RESUME: {P_HIGH_DONE, P_LOW_SUSPENDED} → {P_LOW_RUNNING}
```

```python
P_CPU           = 0   # token = CPU libre
P_HIGH_READY    = 1
P_HIGH_RUNNING  = 2
P_LOW_READY     = 3
P_LOW_RUNNING   = 4
P_LOW_SUSPENDED = 5

transitions = [
    # Despacho normal de alta prioridad
    Transition(consume=[Arc(P_CPU,1), Arc(P_HIGH_READY,1)],
               produce=[Arc(P_HIGH_RUNNING,1)]),

    # Despacho normal de baja prioridad (solo si no hay alta lista)
    Transition(consume=[Arc(P_CPU,1), Arc(P_LOW_READY,1)],
               produce=[Arc(P_LOW_RUNNING,1)]),

    # Preempción: alta prioridad interrumpe a baja
    Transition(consume=[Arc(P_LOW_RUNNING,1), Arc(P_HIGH_READY,1)],
               produce=[Arc(P_LOW_SUSPENDED,1), Arc(P_HIGH_RUNNING,1)]),

    # Reanudación: alta termina, baja retoma CPU
    Transition(consume=[Arc(P_HIGH_RUNNING,1), Arc(P_LOW_SUSPENDED,1)],
               produce=[Arc(P_CPU,1), Arc(P_LOW_RUNNING,1)]),
]
```

**Nota importante**: este modelo describe *cuándo* debe ejecutarse cada tarea,
pero rxnet no ejecuta código en paralelo real. Para ejecución paralela
verdadera, se necesita combinar con hilos del SO o del RTOS. rxnet modela la
**política** de scheduling; el SO implementa el **mecanismo**.

---

**Forma 3: múltiples runtimes tick a distintas frecuencias**

Para sistemas con tareas de períodos muy distintos:

```python
# Tarea de alta frecuencia: control (1 ms)
fast_rt = Runtime()
fast_rt.add_machine(control_machine)

# Tarea de baja frecuencia: monitorización (100 ms)
slow_rt = Runtime()
slow_rt.add_machine(monitor_machine)

tick_count = 0
next_tick = time.monotonic()

while True:
    fast_rt.tick()          # cada 1 ms
    if tick_count % 100 == 0:
        slow_rt.tick()      # cada 100 ms

    tick_count += 1
    next_tick += 0.001
    time.sleep(max(0, next_tick - time.monotonic()))
```

Este patrón es equivalente a un ejecutivo cíclico con **trama mayor** (major
frame) y **tramas menores** (minor frames), el diseño estándar de ejecutivos
cíclicos en aviación (DO-178C) y automoción (AUTOSAR).

---

### 6.4 Resumen comparativo

| Modelo | rxnet cómo lo implementa | Limitación |
|---|---|---|
| **Ejecutivo cíclico** | `rt.tick()` en bucle con `sleep` | Todas las tareas al mismo período |
| **Multitarea cooperativa** | Estado en FSM/PN, comunicación por tokens o señales | Sin preempción real |
| **Prioridad por policy** | Orden de nodos + greedy-sequential PN | Solo modela la política, no el mecanismo |
| **Hilos preemptivos** | rxnet dentro de una tarea RTOS | El SO gestiona la preempción real |
| **Multi-rate** | Múltiples runtimes a distintas frecuencias | Mayor/minor frames manuales |

---

## 7. Cuándo usar FSM, cuándo PN

La pregunta más frecuente: ¿qué modelo uso?

### Usa FSM cuando...

* El módulo tiene **un agente con historia**: hay un único "estado del módulo"
  en cada momento.
* Las transiciones son mutuamente excluyentes: estar en un estado excluye estar
  en otro.
* El control de flujo es lineal (incluso si tiene bucles y ramas).
* Necesitas guards complejos que leen múltiples variables.

```
Semáforo: ROJO → VERDE → AMARILLO → ROJO   ← FSM claro
Cerrojo:  DESBLOQUEADO → BLOQUEADO          ← FSM claro
Protocolo: IDLE → SYN_SENT → ESTABLISHED   ← FSM claro
```

### Usa PN cuando...

* Hay **múltiples agentes independientes** que se sincronizan.
* Los recursos son contables (N slots, M conexiones disponibles).
* El paralelismo es natural: varias actividades ocurren al mismo tiempo.
* Necesitas modelar producción/consumo de forma explícita.

```
Productor/consumidor: P_LLENOS + P_VACIOS   ← PN claro
Semáforo binario: P_MUTEX (0 o 1 token)     ← PN claro
Rendezvous: A espera a B, B espera a A      ← PN claro
```

### Mezclar ambos (patrón habitual)

Lo más potente es combinarlos. La FSM describe la **política** (qué modo estoy);
la PN describe los **recursos y sincronización** (qué tengo disponible):

```python
# Sistema de acceso a sala:
# FSM: gestiona el ciclo de apertura de puerta (CERRADA → ABRIENDO → ABIERTA → CERRANDO)
# PN: gestiona los recursos (tokens = personas dentro, capacidad máxima)

door_machine = Machine(...)  # controla la puerta
capacity_net = Net(...)       # cuenta ocupantes

fsm_rt = Runtime()
fsm_rt.add_machine(door_machine)

pn_rt = Runtime()
pn_rt.add_net(capacity_net)

while True:
    fsm_rt.tick()   # primero: el FSM decide si abrir
    pn_rt.tick()    # después: la PN actualiza el conteo
    time.sleep(0.010)
```

---

## 8. Referencia rápida de la API

### Core runtime

```python
from rxnet.runtime import Context, Runtime

rt = Runtime(inputs=my_inputs)   # inputs opcionales
rt.add_node(node)                # registrar nodo (FSM o PN o custom)
rt.tick()                        # ejecutar un ciclo completo
rt.context                       # acceder al contexto

ctx.inputs                       # entradas en vivo (mutables)
ctx.latched_inputs               # snapshot de este tick (solo lectura)
ctx.enqueue_deferred_action(fn, user)  # encolar acción para fase 5
```

### FSM

```python
from rxnet.fsm import Machine, Transition, Runtime

# Transición mínima (sin guard = siempre dispara)
Transition(from_state=A, to_state=B)

# Transición completa
Transition(from_state=A, to_state=B, guard=my_guard, action=my_action)

# Máquina mínima
Machine(name="m", state=INITIAL, transitions=[...])

# Máquina con callbacks de fase
Machine(
    name="m",
    state=INITIAL,
    transitions=[...],
    user=my_data,
    latch_inputs_cb=read_inputs,   # fase 2: snapshot hardware
    dump_outputs_cb=write_outputs, # fase 6: escribir hardware
)

rt = Runtime()
rt.add_machine(machine)
rt.tick()
```

### Petri Net

```python
from rxnet.pn import Arc, Net, Transition, Runtime

# Arco
Arc(place_id=0, weight=1)    # consume/produce 1 token del lugar 0

# Transición
Transition(
    consume=[Arc(P_A, 1), Arc(P_B, 2)],  # necesita 1 de A y 2 de B
    produce=[Arc(P_C, 1)],               # produce 1 en C
    guard=my_guard,                       # opcional
    action=my_action,                     # deferred, opcional
)

# Red
Net(
    name="n",
    places=[1, 0, 0],          # marcado inicial
    transitions=[...],
    user=my_data,
    latch_inputs_cb=read_inputs,
    dump_outputs_cb=write_outputs,
)

rt = Runtime()
rt.add_net(net)
rt.tick()
```

### Firmas de callbacks

```python
from rxnet.runtime import Context

# Guard: pura, sin efectos, devuelve bool
def my_guard(ctx: Context, user: MyData) -> bool:
    return user.latched_event

# Acción: deferred, corre en fase 5 con estado ya comprometido
def my_action(ctx: Context, user: MyData) -> None:
    user.output = True

# Latch: snapshot hardware, actualizar señales derivadas
def latch_cb(ctx: Context, user: MyData) -> None:
    user.latched_event = read_gpio(user.button_pin)

# Dump: escribir salidas al hardware
def dump_cb(ctx: Context, user: MyData) -> None:
    write_gpio(user.led_pin, user.output)
```

---

## Lectura adicional

* **Código fuente**: `python/rxnet/` — runtime (~80 líneas), fsm (~80 líneas), pn (~130 líneas)
* **Tests**: `python/tests/` — 56 tests que documentan el comportamiento esperado
* **Ejemplos interactivos**: `python/examples/fsm/` y `python/examples/pn/`
* **Especificaciones**: `docs/specs/python/requirements.md` y `docs/specs/python/design.md`
* **Implementación C**: `c/` — misma semántica, API equivalente en C11
