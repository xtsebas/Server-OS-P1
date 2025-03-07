#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <sys/socket.h>   
#include <netinet/in.h>   
#include <arpa/inet.h>    
#include <unistd.h>       

// TODO: cuando backend esté listo, implementar esta clase 
class Server {
public:
    bool connectClient(const std::string& username);  // TODO: Implementar en backend
    size_t getConnectedClients(); // TODO: Implementar en backend
};

// TODO: Esta funcion debe ser reemplazada por una conexion real al servidor
void simulateClient(Server& server, const std::string& username) {
    if (server.connectClient(username)) {
        std::cout << "Cliente " << username << " conectado.\n";
    } else {
        std::cout << "Cliente " << username << " no pudo conectarse (ya existe).\n";
    }
}

// TODO: Esta funcion abrira un socket y se conectara al servidor
void simulateRealClient(const std::string& username, const std::string& server_ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error al crear socket\n";
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error al conectar con el servidor\n";
        close(sock);
        return;
    }

    // TODO: se deberia enviar el nombre de usuario al servidor
    std::string message = "CONNECT " + username;
    send(sock, message.c_str(), message.length(), 0);

    char buffer[1024] = {0};
    recv(sock, buffer, 1024, 0);
    std::cout << "Servidor respondió: " << buffer << "\n";

    close(sock);
}

int main() {
    Server server; // TODO: remplazar con la implementacion real del servidor
    std::vector<std::thread> clientThreads;

    // TODO: Implementar backend real de conexion
    for (int i = 0; i < 10; ++i) {
        clientThreads.emplace_back(simulateClient, std::ref(server), "User" + std::to_string(i));
    }

    for (auto& t : clientThreads) {
        t.join();
    }

    std::cout << "Usuarios conectados: " << server.getConnectedClients() << "\n";
    return 0;
}
