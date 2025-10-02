// Inicializar componentes
        #include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <vector>
#include <string>

// Incluir STB para carregar imagens
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Classe LUT Loader (incluir no header ou inline)
class LUTLoader {
private:
    unsigned int lutTextureID;
    bool isLoaded;
    int width, height, channels;
    
public:
    LUTLoader() : lutTextureID(0), isLoaded(false), width(0), height(0), channels(0) {}
    
    ~LUTLoader() {
        if (isLoaded && lutTextureID != 0) {
            glDeleteTextures(1, &lutTextureID);
        }
    }
    
    bool loadLUT(const std::string& filepath) {
        std::cout << "Carregando LUT: " << filepath << std::endl;
        
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
        
        if (!data) {
            std::cerr << "Erro ao carregar LUT: " << stbi_failure_reason() << std::endl;
            return false;
        }
        
        if (!((width == 1024 && height == 32) || (width == 32 && height == 1024))) {
            std::cerr << "Erro: LUT deve ter dimensões 1024x32 ou 32x1024. Atual: " 
                      << width << "x" << height << std::endl;
            stbi_image_free(data);
            return false;
        }
        
        std::cout << "LUT carregada: " << width << "x" << height << " (" << channels << " canais)" << std::endl;
        
        glGenTextures(1, &lutTextureID);
        glBindTexture(GL_TEXTURE_2D, lutTextureID);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
        GLenum internalFormat = (channels == 4) ? GL_RGBA8 : GL_RGB8;
        
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        
        stbi_image_free(data);
        isLoaded = true;
        
        std::cout << "LUT carregada com sucesso! Texture ID: " << lutTextureID << std::endl;
        return true;
    }
    
    void bindLUT(int textureUnit = 1) {
        if (isLoaded) {
            glActiveTexture(GL_TEXTURE0 + textureUnit);
            glBindTexture(GL_TEXTURE_2D, lutTextureID);
        }
    }
    
    bool getIsLoaded() const { return isLoaded; }
};

// Classe para gerenciar shaders
class Shader {
private:
    unsigned int ID;
    
public:
    Shader(const std::string& vertexSource, const std::string& fragmentSource) {
        // Compilar vertex shader
        unsigned int vertex = compileShader(vertexSource, GL_VERTEX_SHADER);
        
        // Compilar fragment shader  
        unsigned int fragment = compileShader(fragmentSource, GL_FRAGMENT_SHADER);
        
        // Criar programa shader
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        
        // Verificar erros de linking
        int success;
        char infoLog[512];
        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 512, NULL, infoLog);
            std::cerr << "Erro ao linkar shader: " << infoLog << std::endl;
        }
        
        // Deletar shaders (não precisamos mais)
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }
    
    void use() { 
        glUseProgram(ID); 
    }
    
    void setBool(const std::string& name, bool value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
    }
    
    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    
    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }

private:
    unsigned int compileShader(const std::string& source, GLenum type) {
        unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);
        
        // Verificar erros de compilação
        int success;
        char infoLog[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            std::cerr << "Erro ao compilar shader: " << infoLog << std::endl;
        }
        
        return shader;
    }
};

// Classe para capturar tela no Windows
class ScreenCapture {
private:
    HDC hdcScreen, hdcMemDC;
    HBITMAP hbmScreen;
    BITMAP bmpScreen;
    int screenWidth, screenHeight;
    std::vector<unsigned char> pixelData;
    
public:
    ScreenCapture() {
        // Obter contexto da tela
        hdcScreen = GetDC(NULL);
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        // Criar DC compatível
        hdcMemDC = CreateCompatibleDC(hdcScreen);
        hbmScreen = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
        SelectObject(hdcMemDC, hbmScreen);
        
        // Buffer para dados dos pixels
        pixelData.resize(screenWidth * screenHeight * 4); // RGBA
    }
    
    ~ScreenCapture() {
        DeleteObject(hbmScreen);
        DeleteDC(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
    }
    
    bool captureScreen() {
        // Copiar tela para o bitmap
        if (!BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                   hdcScreen, 0, 0, SRCCOPY)) {
            return false;
        }
        
        // Obter informações do bitmap
        GetObject(hbmScreen, sizeof(BITMAP), &bmpScreen);
        
        // Estrutura para informações do bitmap
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bmpScreen.bmWidth;
        bi.biHeight = -bmpScreen.bmHeight; // Top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = 0;
        bi.biClrImportant = 0;
        
        // Obter dados dos pixels
        GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
                  pixelData.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        return true;
    }
    
    unsigned char* getPixelData() { return pixelData.data(); }
    int getWidth() const { return screenWidth; }
    int getHeight() const { return screenHeight; }
};

// Geometria CORRIGIDA para quad fullscreen
float quadVertices[] = {
    // Posições    // Coordenadas de textura (Y invertido corrigido no shader)
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f
};

// Shaders como strings (versão simplificada)
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0); 
    TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
}
)";

// Shader com suporte a LUT
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform sampler2D lutTexture;
uniform bool enableCorrection;
uniform float correctionStrength;
uniform bool useLUT;

vec3 applyLUT3D(vec3 color, sampler2D lut) {
    color = clamp(color, 0.0, 1.0);
    
    const float lutSize = 32.0;
    const float invLutSize = 1.0 / lutSize;
    const float scale = (lutSize - 1.0) * invLutSize;
    const float offset = 0.5 * invLutSize;
    
    vec3 scaledColor = color * (lutSize - 1.0);
    vec3 floorColor = floor(scaledColor);
    vec3 fracColor = scaledColor - floorColor;
    
    float blueSlice = floorColor.b;
    float redCoord = (floorColor.r + 0.5) * invLutSize;
    float greenCoord = (floorColor.g + 0.5) * invLutSize;
    
    vec2 coords1 = vec2(blueSlice * invLutSize + redCoord, greenCoord);
    vec2 coords2 = vec2(min(blueSlice + 1.0, lutSize - 1.0) * invLutSize + redCoord, greenCoord);
    
    vec3 sample1 = texture(lut, coords1).rgb;
    vec3 sample2 = texture(lut, coords2).rgb;
    
    return mix(sample1, sample2, fracColor.b);
}

vec3 hybridCorrection(vec3 color) {
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    float redGreenRatio = color.r / max(color.g, 0.001);
    
    vec3 corrected = color;
    
    if (redGreenRatio > 1.2) {
        corrected.r = min(1.0, color.r * 1.1);
        corrected.b = min(1.0, color.b + (color.r - color.g) * 0.25);
    } else if (redGreenRatio < 0.8) {
        corrected.g = min(1.0, color.g * 1.05);
        corrected.b = min(1.0, color.b + (color.g - color.r) * 0.2);
    }
    
    float newLuminance = dot(corrected, vec3(0.299, 0.587, 0.114));
    if (newLuminance > 0.001) {
        corrected *= luminance / newLuminance;
    }
    
    return clamp(corrected, 0.0, 1.0);
}

void main() {
    vec3 color = texture(screenTexture, TexCoord).rgb;
    
    if (!enableCorrection) {
        FragColor = vec4(color, 1.0);
        return;
    }
    
    vec3 corrected;
    if (useLUT) {
        corrected = applyLUT3D(color, lutTexture);
    } else {
        corrected = hybridCorrection(color);
    }
    
    vec3 final = mix(color, corrected, correctionStrength);
    FragColor = vec4(final, 1.0);
}
)";

class DaltonismoFilter {
private:
    GLFWwindow* window;
    ScreenCapture* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO;
    unsigned int screenTexture;
    
    bool correctionEnabled = false;
    float correctionStrength = 0.6f;
    bool useLUT = false;  // Alternar entre LUT e correção matemática
    
public:
    bool initialize() {
        // Inicializar GLFW
        if (!glfwInit()) {
            std::cerr << "Falha ao inicializar GLFW" << std::endl;
            return false;
        }
        
        // Configurar OpenGL 3.3
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
        
        // Criar janela
        window = glfwCreateWindow(1920, 1080, "Filtro Daltonismo", NULL, NULL);
        if (!window) {
            std::cerr << "Falha ao criar janela GLFW" << std::endl;
            glfwTerminate();
            return false;
        }
        
        glfwMakeContextCurrent(window);
        
        // Carregar OpenGL com GLAD
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Falha ao inicializar GLAD" << std::endl;
            return false;
        }
        
        // Configurar callback de teclas
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        
        // Inicializar componentes
        capture = new ScreenCapture();
        shader = new Shader(vertexShaderSource, fragmentShaderSource);
        lutLoader = new LUTLoader();
        
        // Tentar carregar LUT do arquivo
        if (!lutLoader->loadLUT("luts/deuteranopia_correction.png")) {
            std::cout << "LUT não encontrada, usando correção matemática" << std::endl;
            useLUT = false;
        } else {
            std::cout << "LUT carregada com sucesso!" << std::endl;
            useLUT = true;
        }
        
        setupGeometry();
        setupTexture();
        
        return true;
    }
    
    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            
            // Capturar tela
            capture->captureScreen();
            
            // Atualizar textura
            updateScreenTexture();
            
            // Renderizar
            render();
            
            glfwSwapBuffers(window);
        }
    }
    
    void cleanup() {
        delete capture;
        delete shader;
        delete lutLoader;
        
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteTextures(1, &screenTexture);
        
        glfwTerminate();
    }
    
    // Callback para teclas
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        DaltonismoFilter* filter = (DaltonismoFilter*)glfwGetWindowUserPointer(window);
        
        if (action == GLFW_PRESS) {
            switch (key) {
                case GLFW_KEY_SPACE:
                    filter->correctionEnabled = !filter->correctionEnabled;
                    std::cout << "Correção " << (filter->correctionEnabled ? "ativada" : "desativada") << std::endl;
                    break;
                case GLFW_KEY_EQUAL: // +
                    filter->correctionStrength = std::min(1.0f, filter->correctionStrength + 0.1f);
                    std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
                    break;
                case GLFW_KEY_MINUS: // -
                    filter->correctionStrength = std::max(0.0f, filter->correctionStrength - 0.1f);
                    std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
                    break;
                case GLFW_KEY_L:
                    if (filter->lutLoader->getIsLoaded()) {
                        filter->useLUT = !filter->useLUT;
                        std::cout << "Usando " << (filter->useLUT ? "LUT" : "correção matemática") << std::endl;
                    } else {
                        std::cout << "LUT não disponível" << std::endl;
                    }
                    break;
                case GLFW_KEY_R:
                    // Reset para valores padrão
                    filter->correctionStrength = 0.6f;
                    std::cout << "Configurações resetadas" << std::endl;
                    break;
                case GLFW_KEY_I:
                    // Informações
                    std::cout << "\n=== STATUS ATUAL ===" << std::endl;
                    std::cout << "Correção: " << (filter->correctionEnabled ? "ON" : "OFF") << std::endl;
                    std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
                    std::cout << "Método: " << (filter->useLUT ? "LUT" : "Matemático") << std::endl;
                    if (filter->lutLoader->getIsLoaded()) {
                        std::cout << "LUT carregada: SIM" << std::endl;
                    } else {
                        std::cout << "LUT carregada: NÃO" << std::endl;
                    }
                    std::cout << "===================" << std::endl;
                    break;
                case GLFW_KEY_ESCAPE:
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                    break;
            }
        }
    }

private:
    void setupGeometry() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        
        glBindVertexArray(VAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        
        // Posição
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        // Coordenadas de textura
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    
    void setupTexture() {
        glGenTextures(1, &screenTexture);
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    void updateScreenTexture() {
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
                     capture->getWidth(), capture->getHeight(), 
                     0, GL_BGRA, GL_UNSIGNED_BYTE, capture->getPixelData());
    }
    
    void render() {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        shader->use();
        
        // Configurar uniforms
        shader->setInt("screenTexture", 0);
        shader->setInt("lutTexture", 1);
        shader->setBool("enableCorrection", correctionEnabled);
        shader->setFloat("correctionStrength", correctionStrength);
        shader->setBool("useLUT", useLUT);
        
        // Bind texturas
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        
        if (useLUT && lutLoader->getIsLoaded()) {
            lutLoader->bindLUT(1);
        }
        
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};

int main() {
    std::cout << "=== Filtro para Daltonismo - Deuteranopia ===" << std::endl;
    std::cout << "Controles:" << std::endl;
    std::cout << "ESPAÇO - Ativar/Desativar correção" << std::endl;
    std::cout << "+ / -  - Ajustar intensidade" << std::endl;
    std::cout << "L      - Alternar LUT/Matemático" << std::endl;
    std::cout << "R      - Reset configurações" << std::endl;
    std::cout << "I      - Mostrar informações" << std::endl;
    std::cout << "ESC    - Sair" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Coloque arquivos LUT na pasta luts/" << std::endl;
    std::cout << "Arquivo esperado: deuteranopia_correction.png" << std::endl;
    std::cout << "Formato: 32x1024 ou 1024x32 pixels" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    DaltonismoFilter filter;
    
    if (!filter.initialize()) {
        std::cerr << "Falha ao inicializar o filtro" << std::endl;
        return -1;
    }
    
    filter.run();
    filter.cleanup();
    
    return 0;
}

/*
COMPILAÇÃO NO VISUAL STUDIO:

1. Criar novo projeto Console Application
2. Configurar propriedades (conforme guia anterior)
3. Adicionar este código ao main.cpp
4. Compilar e executar

PRÓXIMOS PASSOS:
1. Integrar LUT do repositório andrewwillmott
2. Melhorar captura de tela (performance)
3. Adicionar interface gráfica
4. Otimizações de performance
*/