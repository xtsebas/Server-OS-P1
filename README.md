# 📡 Servidor de Chat WebSockets

Este es el servidor del sistema de chat basado en WebSockets. Gestiona conexiones de clientes, envía y recibe mensajes, y mantiene el estado de los usuarios.

## 🚀 Características
- Comunicación en tiempo real mediante **WebSockets**
- Registro y autenticación de usuarios por nombre
- Gestión de estados de usuario (Activo, Ocupado, Inactivo, Desconectado)
- Chat público y mensajes privados
- Historial de mensajes en memoria
- Manejo de errores y validaciones

## 📂 Estructura del Proyecto
```
server/
│── src/
│   │── main.cpp                 # Punto de entrada del servidor
│
│── include/                      # Archivos de cabecera
│── tests/                        # Pruebas unitarias
│── config/                       # Configuraciones del servidor
│── docs/                         # Documentación
│── scripts/                      # Scripts auxiliares
│── .gitignore                     # Archivos ignorados por Git
│── README.md                       # Documentación del servidor
```

## 🛠️ Instalación y Ejecución
### 🔧 **Requisitos**
- **C++17 o superior**
- **CMake**
- **Bibliotecas necesarias**: WebSockets (dependiendo de la implementación específica)

### 🚀 **Compilación**
```bash
mkdir build && cd build
cmake ..
make
```

### ▶️ **Ejecutar el servidor**
```bash
./server
```

## 🔄 API del Servidor
El servidor se comunica mediante **mensajes WebSocket en binario** con el siguiente formato:

| Código | Acción | Campos |
|--------|--------|--------|
| 1 | Listar usuarios | - |
| 2 | Obtener usuario | Nombre |
| 3 | Cambiar estado | Nombre, Estado |
| 4 | Enviar mensaje | Destino, Mensaje |
| 5 | Obtener historial | Chat |

## ⚠️ Manejo de Errores
El servidor puede responder con los siguientes errores:
| Código | Descripción |
|--------|------------|
| 1 | Usuario no existe |
| 2 | Estado inválido |
| 3 | Mensaje vacío |
| 4 | Usuario destinatario desconectado |
