# rxnet — Guía de usuario

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
