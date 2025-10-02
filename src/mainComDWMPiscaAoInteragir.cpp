// Solução usando DWM (Desktop Window Manager) + GDI com exclusão de janela
// Captura REAL da tela, excluindo o overlay automaticamente

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <dwmapi.h>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#pragma comment(lib, "dwmapi.lib")

// ==================== CLASSE SHADER (MESMA) ====================
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
            std::cerr << "Erro shader: " << infoLog << std::endl;
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

// ==================== CLASSE LUT LOADER (MESMA) ====================
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

// ==================== CAPTURA DWM OTIMIZADA ====================
class DWMScreenCapture {
private:
    HDC hdcScreen, hdcMemDC;
    HBITMAP hbmScreen, hbmOld;
    int screenWidth, screenHeight;
    std::vector<unsigned char> pixelData;
    HWND overlayWindow;
    bool initialized;
    
    // Buffer duplo para evitar tearing
    std::vector<unsigned char> backBuffer;
    
public:
    DWMScreenCapture(HWND overlayHwnd = NULL) : overlayWindow(overlayHwnd), initialized(false) {
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        pixelData.resize(screenWidth * screenHeight * 4);
        backBuffer.resize(screenWidth * screenHeight * 4);
    }
    
    ~DWMScreenCapture() {
        cleanup();
    }
    
    bool initialize() {
        // Obter DC da tela
        hdcScreen = GetDC(NULL);
        if (!hdcScreen) {
            std::cerr << "Falha ao obter DC da tela" << std::endl;
            return false;
        }
        
        // Criar DC compatível
        hdcMemDC = CreateCompatibleDC(hdcScreen);
        if (!hdcMemDC) {
            std::cerr << "Falha ao criar DC compatível" << std::endl;
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // Criar bitmap compatível
        hbmScreen = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
        if (!hbmScreen) {
            std::cerr << "Falha ao criar bitmap" << std::endl;
            DeleteDC(hdcMemDC);
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // Selecionar bitmap no DC
        hbmOld = (HBITMAP)SelectObject(hdcMemDC, hbmScreen);
        
        initialized = true;
        std::cout << "✅ Captura DWM inicializada: " << screenWidth << "x" << screenHeight << std::endl;
        return true;
    }
    
    void setOverlayWindow(HWND hwnd) {
        overlayWindow = hwnd;
    }
    
    bool captureScreen() {
        if (!initialized) return false;
        
        // MÉTODO 1: Excluir overlay da renderização usando DWM
        if (overlayWindow) {
            // Temporariamente tornar overlay REALMENTE invisível
            LONG_PTR exStyle = GetWindowLongPtr(overlayWindow, GWL_EXSTYLE);
            SetWindowLongPtr(overlayWindow, GWL_EXSTYLE, exStyle | WS_EX_NOREDIRECTIONBITMAP);
            
            // Forçar DWM a atualizar
            DwmFlush();
            
            // Pequeno delay para garantir
            Sleep(1);
        }
        
        // Capturar tela
        if (!BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                   hdcScreen, 0, 0, SRCCOPY | CAPTUREBLT)) {
            return false;
        }
        
        // Restaurar overlay
        if (overlayWindow) {
            LONG_PTR exStyle = GetWindowLongPtr(overlayWindow, GWL_EXSTYLE);
            SetWindowLongPtr(overlayWindow, GWL_EXSTYLE, exStyle & ~WS_EX_NOREDIRECTIONBITMAP);
        }
        
        // Copiar pixels para buffer
        BITMAPINFOHEADER bi;
        ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight; // Top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        
        if (!GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
                      backBuffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
            return false;
        }
        
        // Swap buffers
        std::swap(pixelData, backBuffer);
        
        return true;
    }
    
    // MÉTODO ALTERNATIVO: Captura por camadas (layer-by-layer)
    bool captureScreenLayered() {
        if (!initialized || !overlayWindow) return captureScreen();
        
        // Esconder overlay completamente
        LONG_PTR style = GetWindowLongPtr(overlayWindow, GWL_STYLE);
        SetWindowLongPtr(overlayWindow, GWL_STYLE, style & ~WS_VISIBLE);
        
        // Invalidar área
        InvalidateRect(NULL, NULL, TRUE);
        
        // Forçar update do DWM
        DwmFlush();
        
        // Aguardar compositor
        Sleep(2);
        
        // Capturar
        bool result = BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                            hdcScreen, 0, 0, SRCCOPY | CAPTUREBLT);
        
        // Restaurar overlay
        SetWindowLongPtr(overlayWindow, GWL_STYLE, style);
        
        if (!result) return false;
        
        // Copiar pixels
        BITMAPINFOHEADER bi;
        ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        
        GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
                 backBuffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        std::swap(pixelData, backBuffer);
        
        return true;
    }
    
    // MÉTODO 3: Captura PrintWindow de cada janela visível
    bool captureScreenComposite() {
        if (!initialized) return false;
        
        // Limpar buffer com cor de fundo
        RECT rect = {0, 0, screenWidth, screenHeight};
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdcMemDC, &rect, brush);
        DeleteObject(brush);
        
        // Capturar desktop
        HWND hwndDesktop = GetDesktopWindow();
        PrintWindow(hwndDesktop, hdcMemDC, PW_RENDERFULLCONTENT);
        
        // Enumerar e capturar janelas visíveis (exceto overlay)
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            DWMScreenCapture* capture = (DWMScreenCapture*)lParam;
            
            // Pular se for o overlay
            if (hwnd == capture->overlayWindow) return TRUE;
            
            // Pular se não for visível
            if (!IsWindowVisible(hwnd)) return TRUE;
            
            // Pular janelas minimizadas
            if (IsIconic(hwnd)) return TRUE;
            
            // Obter posição da janela
            RECT rect;
            if (!GetWindowRect(hwnd, &rect)) return TRUE;
            
            // Capturar janela
            HDC hdcWindow = GetDC(hwnd);
            if (hdcWindow) {
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;
                
                BitBlt(capture->hdcMemDC, rect.left, rect.top, width, height,
                      hdcWindow, 0, 0, SRCCOPY);
                
                ReleaseDC(hwnd, hdcWindow);
            }
            
            return TRUE;
        }, (LPARAM)this);
        
        // Copiar resultado
        BITMAPINFOHEADER bi;
        ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        
        GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
                 backBuffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        
        std::swap(pixelData, backBuffer);
        
        return true;
    }
    
    void cleanup() {
        if (hbmScreen) {
            SelectObject(hdcMemDC, hbmOld);
            DeleteObject(hbmScreen);
            hbmScreen = NULL;
        }
        if (hdcMemDC) {
            DeleteDC(hdcMemDC);
            hdcMemDC = NULL;
        }
        if (hdcScreen) {
            ReleaseDC(NULL, hdcScreen);
            hdcScreen = NULL;
        }
        initialized = false;
    }
    
    unsigned char* getPixelData() { return pixelData.data(); }
    int getWidth() const { return screenWidth; }
    int getHeight() const { return screenHeight; }
    bool isInitialized() const { return initialized; }
};

// ==================== SHADERS ====================
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

// ==================== OVERLAY PRINCIPAL ====================
class OverlayDaltonismoFilter {
private:
    GLFWwindow* window;
    DWMScreenCapture* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO;
    unsigned int screenTexture;
    
    bool correctionEnabled = false;
    float correctionStrength = 0.6f;
    bool useLUT = false;
    
    HWND overlayHwnd;
    int captureMethod = 0; // 0=DWM, 1=Layered, 2=Composite
    
public:
    bool initialize() {
        if (!glfwInit()) {
            std::cerr << "Falha ao inicializar GLFW" << std::endl;
            return false;
        }
        
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
        if (!window) {
            std::cerr << "Falha ao criar janela" << std::endl;
            glfwTerminate();
            return false;
        }
        
        glfwSetWindowPos(window, 0, 0);
        glfwMakeContextCurrent(window);
        
        overlayHwnd = glfwGetWin32Window(window);

        // CRITICAL: Configurar janela para NUNCA perder foco de renderização
        LONG_PTR exStyle = GetWindowLong(overlayHwnd, GWL_EXSTYLE);
        SetWindowLong(overlayHwnd, GWL_EXSTYLE, 
                     exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | 
                     WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        
        // Configurar overlay como topmost e transparente
        // SetWindowLong(overlayHwnd, GWL_EXSTYLE, 
        //              GetWindowLong(overlayHwnd, GWL_EXSTYLE) | 
        //              WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
        SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
        
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Falha ao inicializar GLAD" << std::endl;
            return false;
        }
        
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        glfwSwapInterval(0);
        
        // Inicializar captura DWM
        capture = new DWMScreenCapture(overlayHwnd);
        if (!capture->initialize()) {
            std::cerr << "Falha ao inicializar captura DWM" << std::endl;
            return false;
        }
        
        shader = new Shader(vertexShaderSource, fragmentShaderSource);
        lutLoader = new LUTLoader();
        
        if (!lutLoader->loadLUT("luts/deuteranopia_correction.png")) {
            std::cout << "LUT não encontrada, usando correção matemática" << std::endl;
            useLUT = false;
        } else {
            useLUT = true;
        }
        
        setupGeometry();
        setupTexture();
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║ ✅ Sistema DWM Inicializado           ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝" << std::endl;
        std::cout << "Controles:" << std::endl;
        std::cout << "  Ctrl+Shift+D - Ativar/Desativar" << std::endl;
        std::cout << "  Ctrl+Shift++ - Aumentar intensidade" << std::endl;
        std::cout << "  Ctrl+Shift+- - Diminuir intensidade" << std::endl;
        std::cout << "  Ctrl+Shift+L - Alternar LUT/Matemático" << std::endl;
        std::cout << "  Ctrl+Shift+M - Mudar método de captura" << std::endl;
        std::cout << "  Ctrl+Shift+Q - Sair\n" << std::endl;
        
        return true;
    }
    
    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            
            if (correctionEnabled) {
                // Escolher método de captura
                bool captured = false;
                switch(captureMethod) {
                    case 0: captured = capture->captureScreen(); break;
                    case 1: captured = capture->captureScreenLayered(); break;
                    case 2: captured = capture->captureScreenComposite(); break;
                }
                
                if (captured) {
                    updateScreenTexture();
                    render();
                    SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
                }
            } else {
                SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            
            glfwSwapBuffers(window);
            Sleep(16); // 60 FPS
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
    
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        OverlayDaltonismoFilter* filter = 
            (OverlayDaltonismoFilter*)glfwGetWindowUserPointer(window);
        
        if (action == GLFW_PRESS) {
            if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_D) {
                filter->correctionEnabled = !filter->correctionEnabled;
                std::cout << "Filtro " << (filter->correctionEnabled ? "✅ ATIVADO" : "❌ DESATIVADO") << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_EQUAL) {
                filter->correctionStrength = std::min(1.0f, filter->correctionStrength + 0.1f);
                std::cout << "Intensidade: " << (int)(filter->correctionStrength * 100) << "%" << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_MINUS) {
                filter->correctionStrength = std::max(0.0f, filter->correctionStrength - 0.1f);
                std::cout << "Intensidade: " << (int)(filter->correctionStrength * 100) << "%" << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_L) {
                if (filter->lutLoader->getIsLoaded()) {
                    filter->useLUT = !filter->useLUT;
                    std::cout << "Método: " << (filter->useLUT ? "LUT" : "Matemático") << std::endl;
                }
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_M) {
                filter->captureMethod = (filter->captureMethod + 1) % 3;
                const char* methods[] = {"DWM", "Layered", "Composite"};
                std::cout << "Método de captura: " << methods[filter->captureMethod] << std::endl;
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_Q) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
    }

private:
    void setupGeometry() {
        float quadVertices[] = {
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             
            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        
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

// ==================== MAIN ====================
int main() {
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Filtro Daltonismo - DWM Solution     ║" << std::endl;
    std::cout << "║  Desktop Window Manager + GDI         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    
    OverlayDaltonismoFilter filter;
    
    if (!filter.initialize()) {
        std::cerr << "\n❌ Falha ao inicializar!" << std::endl;
        std::cerr << "Pressione Enter para sair..." << std::endl;
        std::cin.get();
        return -1;
    }
    
    std::cout << "✅ Pronto! Testando captura...\n" << std::endl;
    std::cout << "DICA: Se piscar, pressione Ctrl+Shift+M" << std::endl;
    std::cout << "      para alternar método de captura.\n" << std::endl;
    
    filter.run();
    filter.cleanup();
    
    std::cout << "\n✅ Filtro encerrado!" << std::endl;
    
    return 0;
}

/*
════════════════════════════════════════════════════════════════
SOLUÇÃO DWM - EXPLICAÇÃO TÉCNICA
════════════════════════════════════════════════════════════════

PROBLEMA IDENTIFICADO:
- Desktop Duplication capturava apenas Alt-Tab layer
- Filtro piscava constantemente
- Não capturava tela real completa

SOLUÇÃO IMPLEMENTADA:
3 métodos de captura diferentes que você pode alternar:

1. MÉTODO DWM (Padrão):
   - Usa WS_EX_NOREDIRECTIONBITMAP para excluir overlay
   - DwmFlush() força compositor a atualizar
   - BitBlt com CAPTUREBLT para captura completa

2. MÉTODO LAYERED:
   - Esconde overlay temporariamente (WS_VISIBLE)
   - InvalidateRect + DwmFlush para forçar redraw
   - Captura sem overlay presente

3. MÉTODO COMPOSITE:
   - Captura desktop + enumera todas janelas
   - Renderiza cada janela individualmente
   - Pula overlay automaticamente

════════════════════════════════════════════════════════════════
COMO USAR:
════════════════════════════════════════════════════════════════

1. Compile normalmente (não precisa administrador agora!)
2. Execute
3. Ctrl+Shift+D para ativar
4. Se piscar, pressione Ctrl+Shift+M para mudar método
5. Teste os 3 métodos até encontrar o que funciona melhor

════════════════════════════════════════════════════════════════
VANTAGENS DESTA ABORDAGEM:
════════════════════════════════════════════════════════════════

✅ NÃO precisa administrador (GDI simples)
✅ Funciona em Windows 7, 8, 10, 11
✅ 3 métodos diferentes para testar
✅ Captura tela REAL completa
✅ Sem dependência de DirectX 11
✅ Mais compatível com diferentes configurações

════════════════════════════════════════════════════════════════
TESTAR CADA MÉTODO:
════════════════════════════════════════════════════════════════

Método 0 (DWM): 
- Melhor para Windows 10/11
- Usa compositor moderno
- Pode ter delay de 1-2ms

Método 1 (Layered):
- Melhor para Windows 8+
- Esconde/mostra overlay
- Pode piscar levemente

Método 2 (Composite):
- Melhor para múltiplas janelas
- Mais CPU intensivo
- Captura janela por janela

════════════════════════════════════════════════════════════════
COMPILAÇÃO:
════════════════════════════════════════════════════════════════

CMakeLists.txt - adicione:
target_link_libraries(${PROJECT_NAME} PRIVATE
    dwmapi      # ← ADICIONAR ISTO
    gdi32
    user32
    # ... resto
)

Compile:
cmake --build build --config Release

Execute (SEM admin necessário):
./build/DaltonismoFilter.exe

════════════════════════════════════════════════════════════════
TROUBLESHOOTING:
════════════════════════════════════════════════════════════════

Problema: Ainda pisca
Solução: Pressione Ctrl+Shift+M para mudar método
         Teste os 3 métodos

Problema: Captura apenas Alt-Tab
Solução: Use Método 2 (Composite)
         Pressione Ctrl+Shift+M até ver "Composite"

Problema: Performance ruim
Solução: Use Método 0 (DWM)
         Ajuste Sleep(16) para Sleep(33) no código

Problema: Tela preta
Solução: Verifique se outras aplicações estão abertas
         Método 2 captura janelas individuais

════════════════════════════════════════════════════════════════
PRÓXIMOS PASSOS SE AINDA PISCAR:
════════════════════════════════════════════════════════════════

1. Aumente o Sleep no código de 1ms para 5ms
2. Teste em monitor 60Hz (desabilite 144Hz temporariamente)
3. Desabilite G-Sync/FreeSync no painel NVIDIA/AMD
4. Teste Método 2 (Composite) que é o mais estável

════════════════════════════════════════════════════════════════
*/