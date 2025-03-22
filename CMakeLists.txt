cmake_minimum_required(VERSION 3.15)
project(CrowSample)

# Usar C++17 (Requerido por Crow)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Buscar Boost (Asegurar que Boost esté instalado)
find_package(Boost REQUIRED)

# Definiciones necesarias para Crow con Boost Asio
add_definitions(
    -DCROW_ENABLE_BOOST_ASIO      
    -DBOOST_ASIO_NO_DEPRECATED
    -DBOOST_ASIO_HEADER_ONLY
)

# Directorios de inclusión
include_directories(${Boost_INCLUDE_DIRS})

# Definir la ruta de Crow
set(CROW_DIR "${CMAKE_SOURCE_DIR}/crow/Crow-1.2.1-Linux")
include_directories(${CROW_DIR}/include)

# Agregar archivos fuente del servidor
add_executable(CrowSample 
    src/main.cpp 
    src/server.cpp 
    src/websocket_handler.cpp 
    src/logger.cpp
)

# Incluir los directorios de Crow y Boost en el ejecutable
target_include_directories(CrowSample PUBLIC 
    ${Boost_INCLUDE_DIRS}
    ${CROW_DIR}/include
)

# Enlazar librerías necesarias (pthread si Crow lo requiere)
target_link_libraries(CrowSample pthread)
