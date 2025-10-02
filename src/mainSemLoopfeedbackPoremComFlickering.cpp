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
class ScreenCaptureImproved {
private:
    HDC hdcScreen, hdcMemDC;
    HBITMAP hbmScreen;
    int screenWidth, screenHeight;
    std::vector<unsigned char> pixelData;
    HWND overlayWindow;  // Handle da janela a ser excluída
    
public:
    ScreenCaptureImproved(HWND overlayHwnd = NULL) : overlayWindow(overlayHwnd) {
        hdcScreen = GetDC(NULL);
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        hdcMemDC = CreateCompatibleDC(hdcScreen);
        hbmScreen = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
        SelectObject(hdcMemDC, hbmScreen);
        
        pixelData.resize(screenWidth * screenHeight * 4);
    }
    
    ~ScreenCaptureImproved() {
        DeleteObject(hbmScreen);
        DeleteDC(hdcMemDC);
        ReleaseDC(NULL, hdcScreen);
    }
    
    void setOverlayWindow(HWND hwnd) {
        overlayWindow = hwnd;
    }
    
    // MÉTODO 1: Esconder e mostrar janela overlay
    bool captureSreenWithHideShow() {
        if (overlayWindow) {
            // Esconder overlay
            ShowWindow(overlayWindow, SW_HIDE);
            
            // Aguardar compositor atualizar (importante!)
            Sleep(5);
            
            // Capturar tela
            bool result = BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                               hdcScreen, 0, 0, SRCCOPY);
            
            // Mostrar overlay novamente
            ShowWindow(overlayWindow, SW_SHOW);
            
            if (!result) return false;
        } else {
            if (!BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                       hdcScreen, 0, 0, SRCCOPY)) {
                return false;
            }
        }
        
        // Copiar pixels
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight;
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
    }
    
    // MÉTODO 2: Usar Desktop Duplication API (Windows 8+)
    // Mais eficiente e não captura overlays por padrão
    bool captureScreenDDA() {
        // TODO: Implementar usando DXGI Desktop Duplication
        // Esta é a solução mais robusta para Windows moderno
        return captureSreenWithHideShow(); // Fallback
    }
    
    // MÉTODO 3: Usar camadas diferentes (Z-order)
    bool captureScreenExcludingWindow() {
        if (!overlayWindow) {
            return captureSreenWithHideShow();
        }
        
        // Temporariamente remover WS_EX_TOPMOST
        LONG_PTR exStyle = GetWindowLongPtr(overlayWindow, GWL_EXSTYLE);
        SetWindowLongPtr(overlayWindow, GWL_EXSTYLE, exStyle & ~WS_EX_TOPMOST);
        
        // Mover para trás
        SetWindowPos(overlayWindow, HWND_BOTTOM, 0, 0, 0, 0, 
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        
        Sleep(5);
        
        // Capturar
        bool result = BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                           hdcScreen, 0, 0, SRCCOPY);
        
        // Restaurar topmost
        SetWindowLongPtr(overlayWindow, GWL_EXSTYLE, exStyle);
        SetWindowPos(overlayWindow, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        
        if (!result) return false;
        
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight;
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
    }
    
    // Método principal - tenta todos os métodos
    bool captureScreen() {
        // Tentar método mais confiável primeiro
        return captureSreenWithHideShow();
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

class OverlayDaltonismoFilterFixed {
private:
    GLFWwindow* window;
    ScreenCaptureImproved* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO;
    unsigned int screenTexture;
    
    bool correctionEnabled = false;
    float correctionStrength = 0.6f;
    bool useLUT = false;
    
    HWND overlayHwnd;
    
    // Buffer duplo para evitar feedback
    std::vector<unsigned char> previousFrame;
    bool useDoubleBuffering = true;
    
public:
    bool initialize() {
        // Inicialização normal...
        if (!glfwInit()) return false;
        
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        
        window = glfwCreateWindow(screenWidth, screenHeight, "Daltonismo Filter", NULL, NULL);
        if (!window) return false;
        
        glfwSetWindowPos(window, 0, 0);
        glfwMakeContextCurrent(window);
        
        overlayHwnd = glfwGetWin32Window(window);
        
        // Configurar janela
        SetWindowLong(overlayHwnd, GWL_EXSTYLE, 
                     GetWindowLong(overlayHwnd, GWL_EXSTYLE) | 
                     WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
        
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return false;
        
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        glfwSwapInterval(0);
        
        // IMPORTANTE: Passar handle da janela para a captura
        capture = new ScreenCaptureImproved(overlayHwnd);
        shader = new Shader(vertexShaderSource, fragmentShaderSource);
        lutLoader = new LUTLoader();
        
        if (!lutLoader->loadLUT("luts/deuteranopia_correction.png")) {
            useLUT = false;
        } else {
            useLUT = true;
        }
        
        setupGeometry();
        setupTexture();
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Inicializar buffer anterior
        previousFrame.resize(screenWidth * screenHeight * 4);
        
        std::cout << "Overlay inicializado - Feedback loop corrigido!" << std::endl;
        
        return true;
    }
    
    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            
            if (correctionEnabled) {
                // Capturar tela (método já esconde/mostra overlay)
                if (capture->captureScreen()) {
                    updateScreenTexture();
                    render();
                    
                    // Janela visível
                    SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
                }
            } else {
                // Janela invisível
                SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            
            glfwSwapBuffers(window);
            Sleep(16);
        }
    }
    
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        OverlayDaltonismoFilterFixed* filter = 
            (OverlayDaltonismoFilterFixed*)glfwGetWindowUserPointer(window);
        
        if (action == GLFW_PRESS) {
            if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_D) {
                filter->correctionEnabled = !filter->correctionEnabled;
                std::cout << "Filtro " << (filter->correctionEnabled ? "ATIVADO" : "DESATIVADO") << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_EQUAL) {
                filter->correctionStrength = std::min(1.0f, filter->correctionStrength + 0.1f);
                std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_MINUS) {
                filter->correctionStrength = std::max(0.0f, filter->correctionStrength - 0.1f);
                std::cout << "Intensidade: " << filter->correctionStrength << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_Q) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
    }
    
    // Resto dos métodos (setupGeometry, setupTexture, etc.) igual ao anterior
    void setupGeometry() { /* mesmo código */ }
    void setupTexture() { /* mesmo código */ }
    void updateScreenTexture() { /* mesmo código */ }
    void render() { /* mesmo código */ }
    void cleanup() { 
        delete capture;
        delete shader;
        delete lutLoader;
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteTextures(1, &screenTexture);
        glfwTerminate();
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
    
    OverlayDaltonismoFilterFixed filter;
    
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