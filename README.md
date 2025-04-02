# Servidor de Chat WebSocket — Sistemas Operativos UVG

Este es el **servidor** del sistema de chat para el proyecto de Sistemas Operativos 2025, desarrollado en **C++17** usando el framework **Crow**. La comunicación se realiza mediante **WebSockets en binario**, cumpliendo un protocolo estricto definido para interoperabilidad entre distintos grupos.

---

## Características

- Comunicación **en tiempo real** vía WebSockets
- Manejo de **estados de usuario**: ACTIVO, OCUPADO, INACTIVO, DESCONECTADO
- Soporte para **mensajes públicos y privados**
- **Historial** de conversaciones en memoria
- **Reconexión** automática si el usuario se desconecta
- **Notificaciones** de ingreso, estado y desconexión
- Validaciones estrictas y **manejo de errores**
- Registro detallado en consola mediante `Logger`

---

## Estructura del Proyecto

```
Server-OS-P1/
├── src/
│   ├── main.cpp                  # Punto de entrada del servidor
│   ├── websocket_handler.cpp     # Lógica principal del protocolo
│   ├── logger.cpp
├── include/
│   ├── websocket_handler.h
│   ├── logger.h
│   ├── websocket_global.h
├── tests/
│   └── test_server.cpp           # Pruebas automáticas
├── config/
├── CMakeLists.txt
```

---

## Requisitos

- C++17 o superior
- CMake
- Bibliotecas:
  - Crow (incluye WebSocket)
  - pthread (para hilos)
  - STL

---

## Compilación

```bash
mkdir build && cd build
cmake ..
make
```

Esto generará el ejecutable `Server`.

---

## Ejecución del Servidor

```bash
./Server
```

El servidor queda escuchando en:

```
ws://localhost:18080/ws?name=usuario
```

Si el cliente no proporciona el nombre, la conexión será rechazada.

---

## Protocolo Binario

Todos los mensajes enviados y recibidos son estrictamente binarios y siguen este formato:

### Cliente → Servidor

| Opcode | Acción             |
|--------|--------------------|
|   1   | Listar usuarios    |
| 2   | Obtener info       |
| 3   | Cambiar estado     |
| 4   | Enviar mensaje     |
| 5   | Obtener historial  |

### Servidor → Cliente

| Opcode | Acción                |
|--------|------------------------|
| 50   | Error                  |
| 51   | Lista de usuarios      |
| 52   | Info usuario           |
| 53   | Notificación de ingreso|
| 54   | Cambio de estado       |
| 55   | Mensaje recibido       |
| 56   | Historial              |

---

## Pruebas Automáticas

Para correr las pruebas:

```bash
make TestServer
./TestServer
```

Se evalúan:
- Registro y duplicados
- Listado de usuarios
- Consulta de información
- Cambio de estado
- Envío de mensajes
- Historial

---

## Notas Finales

- Todos los mensajes que no sean binarios son ignorados.
- Se requiere `?name=usuario` en la conexión WebSocket.
- Se incluye manejo de hilos para monitoreo de inactividad y limpieza de conexiones.
- Puede interoperar con clientes hechos en Boost, Qt, JS, Python, etc., siempre que respeten el protocolo binario.

---

Desarrollado para el curso **Sistemas Operativos - Universidad del Valle de Guatemala**.
