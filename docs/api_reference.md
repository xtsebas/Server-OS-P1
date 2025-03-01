# 📖 API Reference - Servidor de Chat WebSockets

Este documento describe la API del servidor WebSockets para la aplicación de chat.

## 📡 Conexión al Servidor
Para conectarse al servidor, los clientes deben usar la siguiente URL:
```
ws://<host>:<port>?name=<username>
```
**Parámetros:**
- `<host>`: Dirección del servidor.
- `<port>`: Puerto en el que está corriendo el servidor.
- `<username>`: Nombre del usuario que se desea registrar.

⚠️ **Restricciones:**
- El nombre de usuario no puede ser vacío ni `~` (reservado para el chat general).
- Si el usuario ya está en línea, la conexión será rechazada.
- Si el usuario existía pero estaba desconectado, se aceptará la conexión.

---

## 📩 Mensajes del Cliente al Servidor
| Código | Acción | Descripción | Campos |
|--------|--------|-------------|---------|
| 1 | Listar usuarios | Obtiene la lista de usuarios conectados. | - |
| 2 | Obtener usuario | Consulta información de un usuario. | Nombre |
| 3 | Cambiar estado | Modifica el estado del usuario. | Nombre, Estado |
| 4 | Enviar mensaje | Envía un mensaje a otro usuario o al chat general. | Destino, Mensaje |
| 5 | Obtener historial | Solicita el historial de un chat. | Chat |

---

## 📩 Mensajes del Servidor al Cliente
| Código | Acción | Descripción | Campos |
|--------|--------|-------------|---------|
| 50 | Error | Devuelve un código de error. | Código de error |
| 51 | Listar usuarios | Responde con la lista de usuarios conectados. | Número de usuarios, Lista de usuarios |
| 52 | Información usuario | Devuelve los datos de un usuario. | Nombre, Estado |
| 53 | Usuario registrado | Notifica a todos sobre un nuevo usuario. | Nombre, Estado |
| 54 | Cambio de estado | Informa a todos sobre un cambio de estado. | Nombre, Estado |
| 55 | Mensaje recibido | Notifica a los destinatarios sobre un mensaje nuevo. | Remitente, Mensaje |
| 56 | Historial de chat | Devuelve el historial de mensajes de un chat. | Lista de mensajes |

---

## ❌ Manejo de Errores
El servidor puede devolver los siguientes errores:

| Código | Descripción |
|--------|------------|
| 1 | Usuario no existe. |
| 2 | Estado inválido. |
| 3 | Mensaje vacío. |
| 4 | Usuario destinatario desconectado. |

---

## 🛠 Ejemplo de Mensaje
### **Ejemplo: Cliente solicita la lista de usuarios**
**Solicitud:**
```
[1]
```
**Respuesta del servidor:**
```
[51, 2, "Jo", 1, "Elo", 2]
```
*(Dos usuarios conectados: "Jo" (Activo) y "Elo" (Ocupado))*

---