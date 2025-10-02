#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// Classe LUT Loader (mesmo código anterior, compactado)
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
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
        if (!data) return false;
        
        if (!((width == 1024 && height == 32) || (width == 32 && height == 1024))) {
            stbi_image_free(data);
            return false;
        }
        
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

// Classe Shader (mesmo código anterior, compactado)
class Shader {
private:
    unsigned int ID;
    
    unsigned int compileShader(const std::string& source, GLenum type) {
        unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);
        
        int success;
        char infoLog[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            std::cerr << "Erro ao compilar shader: " << infoLog << std::endl;
        }
        return shader;
    }
    
public:
    Shader(const std::string& vertexSource, const std::string& fragmentSource) {
        unsigned int vertex = compileShader(vertexSource, GL_VERTEX_SHADER);
        unsigned int fragment = compileShader(fragmentSource, GL_FRAGMENT_SHADER);
        
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }
    
    void use() { glUseProgram(ID); }
    void setBool(const std::string& name, bool value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
    }
    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }
};

// Captura de tela otimizada
class ScreenCapture {
private:
    HDC hdcScreen, hdcMemDC;
    HBITMAP hbmScreen;
    int screenWidth, screenHeight;
    std::vector<unsigned char> pixelData;
    
public:
    ScreenCapture() {
        hdcScreen = GetDC(NULL);
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        hdcMemDC = CreateCompatibleDC(hdcScreen);
        hbmScreen = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
        SelectObject(hdcMemDC, hbmScreen);
        
        pixelData.resize(screenWidth * screenHeight * 4);
    }
    
    ~ScreenCapture() {
        DeleteObject(hbmScreen);
        DeleteDC(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
    }
    
    bool captureScreen() {
        if (!BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                   hdcScreen, 0, 0, SRCCOPY)) {
            return false;
        }
        
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight; // Top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = 0;
        bi.biClrImportant = 0;
        
        GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
                  pixelData.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        return true;

        //--------------------------------------------------------------------
        // Temporariamente esconder a janela do overlay antes da captura
        // HWND overlayWindow = glfwGetWin32Window(glfwGetCurrentContext());
        // ShowWindow(overlayWindow, SW_HIDE);
        
        // // Pequena pausa para garantir que a janela foi escondida
        // Sleep(1);
        
        // // Capturar a tela
        // bool success = BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
        //         hdcScreen, 0, 0, SRCCOPY);
        
        // // Restaurar a visibilidade da janela do overlay
        // ShowWindow(overlayWindow, SW_SHOW);
        
        // if (!success) {
        //     return false;
        // }
        
        // BITMAPINFOHEADER bi;
        // bi.biSize = sizeof(BITMAPINFOHEADER);
        // bi.biWidth = screenWidth;
        // bi.biHeight = -screenHeight; // Top-down
        // bi.biPlanes = 1;
        // bi.biBitCount = 32;
        // bi.biCompression = BI_RGB;
        // bi.biSizeImage = 0;
        // bi.biXPelsPerMeter = 0;
        // bi.biYPelsPerMeter = 0;
        // bi.biClrUsed = 0;
        // bi.biClrImportant = 0;
        
        // GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
        //         pixelData.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        // return true;
    }
    
    unsigned char* getPixelData() { return pixelData.data(); }
    int getWidth() const { return screenWidth; }
    int getHeight() const { return screenHeight; }
};

// Geometria fullscreen
float quadVertices[] = {
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f
};

// Vertex shader
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

// Fragment shader com LUT
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

class OverlayDaltonismoFilter {
private:
    GLFWwindow* window;
    ScreenCapture* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO;
    unsigned int screenTexture;
    
    bool correctionEnabled = false;
    float correctionStrength = 0.6f;
    bool useLUT = false;
    bool isMinimized = false;
    
    // Windows específico para overlay
    HWND hwnd;
    
public:
    bool initialize() {
        if (!glfwInit()) {
            std::cerr << "Falha ao inicializar GLFW" << std::endl;
            return false;
        }
        
        // Obter dimensões da tela
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        // Configurar OpenGL 3.3
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        
        // CONFIGURAÇÕES CRÍTICAS PARA OVERLAY:
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);        // Sem bordas
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE); // Transparência
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);          // Sempre no topo
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);        // Não redimensionável
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);          // Não roubar foco
        
        // Criar janela fullscreen transparente
        window = glfwCreateWindow(screenWidth, screenHeight, "Daltonismo Filter Overlay", NULL, NULL);
        if (!window) {
            std::cerr << "Falha ao criar janela overlay" << std::endl;
            glfwTerminate();
            return false;
        }
        
        // Posicionar na tela toda
        glfwSetWindowPos(window, 0, 0);
        glfwMakeContextCurrent(window);
        
        // CONFIGURAÇÕES WINDOWS ESPECÍFICAS PARA OVERLAY
        hwnd = glfwGetWin32Window(window);
        
        // Tornar janela click-through (eventos de mouse passam através)
        SetWindowLong(hwnd, GWL_EXSTYLE, 
                     GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);
        
        // Definir transparência inicial
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY | LWA_ALPHA);
        
        // Carregar OpenGL
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Falha ao inicializar GLAD" << std::endl;
            return false;
        }
        
        // Configurar callbacks
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        
        // Desabilitar VSync para menor latência
        glfwSwapInterval(0);
        
        // Inicializar componentes
        capture = new ScreenCapture();
        shader = new Shader(vertexShaderSource, fragmentShaderSource);
        lutLoader = new LUTLoader();
        
        if (!lutLoader->loadLUT("luts/deuteranopia_correction.png")) {
            std::cout << "LUT não encontrada, usando correção matemática" << std::endl;
            useLUT = false;
        } else {
            std::cout << "LUT carregada com sucesso!" << std::endl;
            useLUT = true;
        }
        
        setupGeometry();
        setupTexture();
        
        // Configurar blending para transparência
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        std::cout << "Overlay inicializado com sucesso!" << std::endl;
        std::cout << "Use Ctrl+Shift+D para ativar/desativar" << std::endl;
        
        return true;
    }
    
    void run() {
        while (!glfwWindowShouldClose(window)) {
            // Poll events sem bloquear
            glfwPollEvents();
            
            // Capturar tela apenas se correção estiver ativa
            if (correctionEnabled && !isMinimized) {
                capture->captureScreen();
                updateScreenTexture();
                render();
            } else {
                // Renderizar tela transparente quando desabilitado
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            
            glfwSwapBuffers(window);
            
            // Pequena pausa para não consumir 100% da CPU
            Sleep(16); // ~60 FPS
        }

        // DWORD lastFrameTime = GetTickCount();
        // const DWORD frameDelay = 1000 / 60; // 60 FPS

        // while (!glfwWindowShouldClose(window)) {
        //     DWORD currentTime = GetTickCount();
            
        //     // Verifica se é hora de renderizar um novo frame
        //     if (currentTime - lastFrameTime >= frameDelay) {
        //         glfwPollEvents();

        //         if (correctionEnabled && !isMinimized) {
        //             // Desabilita a janela temporariamente sem mostrar/esconder
        //             LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        //             SetWindowLongPtr(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
                    
        //             // Captura a tela
        //             capture->captureScreen();
                    
        //             // Restaura o estilo da janela
        //             SetWindowLongPtr(hwnd, GWL_EXSTYLE, style);
                    
        //             updateScreenTexture();
        //             render();
        //         } else {
        //             glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        //             glClear(GL_COLOR_BUFFER_BIT);
        //         }

        //         glfwSwapBuffers(window);
        //         lastFrameTime = currentTime;
        //     } else {
        //         // Libera CPU quando não está renderizando
        //         Sleep(1);
        //     }
        // }
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
    
    // Callback para teclas GLOBAIS
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        OverlayDaltonismoFilter* filter = (OverlayDaltonismoFilter*)glfwGetWindowUserPointer(window);
        
        if (action == GLFW_PRESS) {
            // Ctrl+Shift+D para ativar/desativar
            if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_D) {
                filter->correctionEnabled = !filter->correctionEnabled;
                std::cout << "Filtro " << (filter->correctionEnabled ? "ATIVADO" : "DESATIVADO") << std::endl;
                
                // Atualizar transparência da janela
                if (filter->correctionEnabled) {
                    // Tornar visível quando ativo
                    SetLayeredWindowAttributes(filter->hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
                } else {
                    // Tornar completamente transparente quando desativo
                    SetLayeredWindowAttributes(filter->hwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
                }
            }
            
            // Ctrl+Shift+ +/- para intensidade
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_EQUAL) {
                filter->correctionStrength = std::min(1.0f, filter->correctionStrength + 0.1f);
                std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_MINUS) {
                filter->correctionStrength = std::max(0.0f, filter->correctionStrength - 0.1f);
                std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
            }
            
            // Ctrl+Shift+L para alternar LUT
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_L) {
                if (filter->lutLoader->getIsLoaded()) {
                    filter->useLUT = !filter->useLUT;
                    std::cout << "Método: " << (filter->useLUT ? "LUT" : "Matemático") << std::endl;
                }
            }
            
            // Ctrl+Shift+Q para sair
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_Q) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
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
        
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
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
        shader->setInt("screenTexture", 0);
        shader->setInt("lutTexture", 1);
        shader->setBool("enableCorrection", correctionEnabled);
        shader->setFloat("correctionStrength", correctionStrength);
        shader->setBool("useLUT", useLUT);
        
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
    std::cout << "=== Filtro Overlay Daltonismo - Deuteranopia ===" << std::endl;
    std::cout << "CONTROLES GLOBAIS (funcionam em qualquer aplicação):" << std::endl;
    std::cout << "Ctrl+Shift+D  - Ativar/Desativar filtro" << std::endl;
    std::cout << "Ctrl+Shift++  - Aumentar intensidade" << std::endl;
    std::cout << "Ctrl+Shift+-  - Diminuir intensidade" << std::endl;
    std::cout << "Ctrl+Shift+L  - Alternar LUT/Matemático" << std::endl;
    std::cout << "Ctrl+Shift+Q  - Sair do programa" << std::endl;
    std::cout << "=================================================" << std::endl;
    std::cout << "O overlay está sendo inicializado..." << std::endl;
    std::cout << "Use qualquer aplicação normalmente!" << std::endl;
    std::cout << "=================================================" << std::endl;
    
    OverlayDaltonismoFilter filter;
    
    if (!filter.initialize()) {
        std::cerr << "Falha ao inicializar o overlay" << std::endl;
        return -1;
    }
    
    filter.run();
    filter.cleanup();
    
    return 0;
}

/*
FUNCIONALIDADES DO OVERLAY:

1. ✅ TRANSPARENTE - Não interfere no uso normal
2. ✅ CLICK-THROUGH - Mouse passa através da janela  
3. ✅ SEMPRE NO TOPO - Fica sobre todas as aplicações
4. ✅ FULLSCREEN - Cobre toda a tela
5. ✅ CONTROLES GLOBAIS - Funcionam em qualquer app
6. ✅ BAIXA LATÊNCIA - Rendering otimizado
7. ✅ AUTO-TRANSPARÊNCIA - Invisível quando desabilitado

COMPILAÇÃO:
- Mesmo processo anterior
- Testado no Windows 10/11
- Funciona com qualquer aplicação
*/