# 🔌 Protocolo WebSockets - Servidor de Chat

Este documento describe el protocolo utilizado en el servidor de chat basado en WebSockets.

## 📡 ¿Qué es WebSockets?
WebSockets es un protocolo de comunicación bidireccional que permite a clientes y servidores enviar y recibir mensajes de manera continua sin necesidad de reabrir una conexión cada vez.

## 📌 Flujo General de la Aplicación
1. **Conexión del Cliente** → Un usuario se conecta al servidor mediante WebSockets.
2. **Intercambio de Mensajes** → Cliente y servidor envían mensajes según el protocolo definido.
3. **Desconexión** → Un usuario se desconecta y su estado se actualiza en el servidor.

---

## 🏁 Estados de un Usuario
Cada usuario conectado al servidor tiene un estado asociado:
- **Activo** → Ha enviado mensajes recientemente.
- **Inactivo** → Está conectado pero no ha interactuado en un tiempo.
- **Ocupado** → No desea recibir notificaciones de mensajes en la interfaz.
- **Desconectado** → Ha cerrado la conexión, pero su información se mantiene en el servidor.

---

## 📩 Formato de Mensajes
Cada mensaje sigue una estructura binaria específica:

| **Campo** | **Tamaño** | **Descripción** |
|-----------|-----------|----------------|
| Código de Mensaje | 1 byte | Identifica la acción (Ejemplo: 1 = Listar usuarios, 4 = Enviar mensaje) |
| Tamaño de Campo | 1 byte | Indica la longitud del campo siguiente |
| Datos | Variable | Contenido del mensaje |

⚠ **Nota:** Todos los argumentos deben enviarse en el orden especificado.

---

## 📡 Ejemplo de Mensajes

### **Enviar un mensaje a otro usuario**
```
[4] [2] ["Jo"] [5] ["Hola"]
```
Donde:
- `4` → Código de acción (Enviar mensaje).
- `2` → Longitud del nombre del usuario destino (`Jo`).
- `Jo` → Usuario que recibirá el mensaje.
- `5` → Longitud del mensaje.
- `Hola` → Contenido del mensaje.

### **Respuesta del Servidor al Enviar un Mensaje**
```
[55] [2] ["Jo"] [5] ["Hola"]
```
Donde:
- `55` → Código de acción (Confirmación de mensaje recibido).
- `Jo` → Usuario remitente.
- `Hola` → Mensaje enviado.

---

## 🚦 Manejo de Estados
Un usuario puede cambiar su estado con el siguiente mensaje:
```
[3] [2] ["Jo"] [1]
```
Donde:
- `3` → Código de acción (Cambiar estado).
- `2` → Longitud del nombre (`Jo`).
- `Jo` → Usuario que cambia de estado.
- `1` → Nuevo estado (1 = Activo, 2 = Ocupado, 3 = Inactivo, 0 = Desconectado).

---

## 🔄 Resumen de Código de Mensajes
| **Código** | **Acción** |
|------------|-----------|
| 1 | Listar usuarios |
| 2 | Obtener usuario |
| 3 | Cambiar estado |
| 4 | Enviar mensaje |
| 5 | Obtener historial de mensajes |
| 50 | Error |
| 51 | Lista de usuarios |
| 52 | Información de usuario |
| 53 | Nuevo usuario registrado |
| 54 | Cambio de estado |
| 55 | Mensaje recibido |
| 56 | Historial de chat |

---