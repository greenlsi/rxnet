# Using rxnet as a Dependency

Este documento explica cómo integrar rxnet como una librería en tus proyectos C/C++.

## Opción 1: Instalación del Sistema

### Paso 1: Compilar e Instalar rxnet

```bash
cd c/
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build .
make test
cmake --install .
```

### Paso 2: Usar en tu proyecto

En el `CMakeLists.txt` de tu proyecto:

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_project)

# Buscar rxnet en el sistema
find_package(rxnet REQUIRED)

# Tu ejecutable
add_executable(my_app main.c)

# Enlazar con rxnet
target_link_libraries(my_app PRIVATE rxnet::rxnet)
```

Compilar tu proyecto:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Opción 2: Usar rxnet como Subdirectorio

### Paso 1: Estructura del proyecto

```
my_project/
├── CMakeLists.txt
├── src/
│   └── main.c
└── rxnet/          # Submódulo git o copia local
    └── c/
```

### Paso 2: CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_project)

# Añadir rxnet como subdirectorio
add_subdirectory(rxnet/c rxnet_build)

# Tu ejecutable
add_executable(my_app src/main.c)

# Enlazar con rxnet (sin namespace cuando se usa add_subdirectory)
target_link_libraries(my_app PRIVATE rxnet)
```

---

## Opción 3: Usar rxnet como Paquete Externo

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_project)

include(FetchContent)

FetchContent_Declare(rxnet
    GIT_REPOSITORY https://github.com/greenlsi/rxnet.git
    GIT_TAG main
    SOURCE_SUBDIR c
)

FetchContent_MakeAvailable(rxnet)

add_executable(my_app src/main.c)
target_link_libraries(my_app PRIVATE rxnet::rxnet)
```

## Instalación Personalizada

Para instalar en un directorio diferente:

```bash
cd c/
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --install .
```

Luego, para encontrarla, usa:

```bash
cmake .. -DCMAKE_PREFIX_PATH=$HOME/.local
```

---

## Opciones de Compilación

Al compilar rxnet, tienes varias opciones:

| Opción | Valor | Descripción |
|--------|-------|-------------|
| `BUILD_TESTS` | ON/OFF | Compilar y ejecutar los tests |
| `RX_TRACE_ENABLE` | ON/OFF | Habilitar tracing |

Ejemplo:

```bash
cmake .. -DBUILD_TESTS=ON
```
