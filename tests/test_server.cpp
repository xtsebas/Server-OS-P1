#include <gtest/gtest.h>
#include <string>

// TODO: Cuando el backend esté listo, debes implementar esta clase
class Server {
public:
    bool connectClient(const std::string& username);  // TODO: Implementar en backend 
    bool disconnectClient(const std::string& username); // TODO: Implementar en backend 
    size_t getConnectedClients(); // TODO: Implementar en backend 
};

// TODO: con backend.
TEST(ServerTest, ClientConnectionTest) {
    Server server;  // TODO: Esto debe ser una instancia del servidor real

    EXPECT_TRUE(server.connectClient("User1")); // TODO: enviar solicitud de conexion al backend
    EXPECT_TRUE(server.connectClient("User2"));
    EXPECT_FALSE(server.connectClient("User1")); // No debe permitir duplicados

    EXPECT_EQ(server.getConnectedClients(), 2);
}

// TODO: En el backend real, usa close para cerrar la conexión del cliente.
TEST(ServerTest, ClientDisconnectionTest) {
    Server server;
    server.connectClient("User1");
    server.connectClient("User2");

    EXPECT_TRUE(server.disconnectClient("User1"));
    EXPECT_FALSE(server.disconnectClient("User3")); // No existe este usuario

    EXPECT_EQ(server.getConnectedClients(), 1);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
