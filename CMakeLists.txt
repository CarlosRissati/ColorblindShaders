cmake_minimum_required(VERSION 3.16)
project(DaltonismoFilter VERSION 1.0.0 LANGUAGES C CXX)


# Configurações do C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Gerar compile_commands.json para IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configurações de debug/release
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
    add_compile_definitions(DEBUG)
else()
    add_compile_options(-O3 -DNDEBUG)
endif()

# Encontrar pacotes via vcpkg
# Defina o caminho para o diretório onde glfw3Config.cmake está localizado, por exemplo, via vcpkg:
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};Indique a pasta x64-mingw-dynamic aqui EX: -> (C:/vcpkg/installed/x64-mingw-dynamic)")
# Ou defina glfw3_DIR diretamente:
# set(glfw3_DIR "C:/path/to/vcpkg/installed/x64-windows/share/glfw3")

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# STB para carregar imagens (header-only)
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/stb")
if(NOT EXISTS ${STB_DIR})
    file(MAKE_DIRECTORY ${STB_DIR})
    # Baixar STB automaticamente se não existir
    file(DOWNLOAD 
       
  "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
         "${STB_DIR}/stb_image.h")
endif()

# GLAD será incluído manualmente
set(GLAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glad")
add_library(glad STATIC
    ${GLAD_DIR}/src/glad.c
    ${GLAD_DIR}/include/glad/glad.h
    ${GLAD_DIR}/include/KHR/khrplatform.h
)
target_include_directories(glad PUBLIC ${GLAD_DIR}/include)
set_target_properties(glad PROPERTIES LINKER_LANGUAGE C)

# Headers personalizados
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/stb)

# Definir executável
add_executable(${PROJECT_NAME}
    src/main.cpp
    # Adicionar outros arquivos .cpp aqui quando criar
)

# Linkar bibliotecas
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glm::glm
    glad
    opengl32
    gdi32
    user32
    kernel32
    dwmapi
    d3d11
    dxgi
)

# Configurações específicas do Windows
if(WIN32)
    
target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        GLFW_INCLUDE_NONE
    )
endif()

# Configurações de runtime (importante para MinGW)
if(MINGW)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-static-libgcc -static-libstdc++"
    )
endif()

# Copiar shaders e recursos para build directory
file(COPY ${CMAKE_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/luts DESTINATION ${CMAKE_BINARY_DIR})

# Criar diretório de output se não existir
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/output)

# Mensagens de status
message(STATUS "Projeto: ${PROJECT_NAME}")
message(STATUS "Versão: ${PROJECT_VERSION}")
message(STATUS "Compilador: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "vcpkg root: ${VCPKG_INSTALLED_DIR}")

# Alvos customizados
add_custom_target(run
    COMMAND ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe
   
 DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Executando ${PROJECT_NAME}"
)

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Limpando todos os arquivos de build"
)