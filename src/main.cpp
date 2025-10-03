#define _WIN32_WINNT 0x0A00

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#pragma comment(lib, "dwmapi.lib")

using namespace std::chrono;

// IDs dos hotkeys globais
#define HOTKEY_TOGGLE 1
#define HOTKEY_INCREASE 2
#define HOTKEY_DECREASE 3
#define HOTKEY_METHOD 4
#define HOTKEY_QUIT 5

// Forward declaration
class FinalOverlayFilter;

// Instância global (será definida depois)
FinalOverlayFilter* g_filterInstance = nullptr;
WNDPROC g_originalWndProc = nullptr;

// ==================== CLASSE SHADER ====================
class Shader {
private:
    unsigned int ID;
    
    unsigned int compileShader(const std::string& source, GLenum type) {
        unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);
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

// ==================== CLASSE LUT LOADER ====================
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

// ==================== CAPTURA INDEPENDENTE ====================
class IndependentScreenCapture {
private:
    HDC hdcScreen, hdcMemDC;
    HBITMAP hbmScreen, hbmOld;
    int screenWidth, screenHeight;
    // HWND overlayHwnd;
    // std::atomic<bool>* correctionEnabled;
    
    std::vector<unsigned char> frontBuffer;
    std::vector<unsigned char> backBuffer;
    
    std::mutex bufferMutex;
    std::thread captureThread;
    std::atomic<bool> running;
    std::atomic<bool> initialized;
    std::atomic<int> frameCount;
    
public:
    IndependentScreenCapture() 
        : running(false), initialized(false), frameCount(0) {
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        frontBuffer.resize(screenWidth * screenHeight * 4);
        backBuffer.resize(screenWidth * screenHeight * 4);
    }
    
    ~IndependentScreenCapture() {
        stop();
        cleanup();
    }
    
    bool initialize() {
        hdcScreen = GetDC(NULL);
        if (!hdcScreen) return false;
        
        hdcMemDC = CreateCompatibleDC(hdcScreen);
        if (!hdcMemDC) {
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        hbmScreen = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
        if (!hbmScreen) {
            DeleteDC(hdcMemDC);
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        hbmOld = (HBITMAP)SelectObject(hdcMemDC, hbmScreen);
        
        initialized = true;
        std::cout << "✅ Captura independente: " << screenWidth << "x" << screenHeight << std::endl;
        return true;
    }
    
    void start() {
        if (running) return;
        
        running = true;
        captureThread = std::thread(&IndependentScreenCapture::captureLoop, this);
        
        // CRITICAL: Aumentar prioridade da thread
        HANDLE threadHandle = (HANDLE)captureThread.native_handle();
        SetThreadPriority(threadHandle, THREAD_PRIORITY_HIGHEST);
            
        std::cout << "✅ Thread de captura em ALTA PRIORIDADE" << std::endl;
    }
    
    void stop() {
        if (!running) return;
        running = false;
        if (captureThread.joinable()) {
            captureThread.join();
        }
    }
    
    unsigned char* getPixelData() {
        std::lock_guard<std::mutex> lock(bufferMutex);
        return frontBuffer.data();
    }

    // void setOverlayWindow(HWND hwnd) {
    //     overlayHwnd = hwnd;
    // }

    // void setCorrectionState(std::atomic<bool>* state) {
    //     correctionEnabled = state;
    // }
    
    int getWidth() const { return screenWidth; }
    int getHeight() const { return screenHeight; }
    bool isInitialized() const { return initialized; }
    int getFrameCount() const { return frameCount; }
    
private:
    void captureLoop() {
        BITMAPINFOHEADER bi;
        ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = screenWidth;
        bi.biHeight = -screenHeight;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        
        auto lastCapture = steady_clock::now();
        
        while (running) {
            auto now = steady_clock::now();
            auto elapsed = duration_cast<milliseconds>(now - lastCapture).count();
            
            // if (overlayHwnd) ShowWindow(overlayHwnd, SW_HIDE);

            // Capturar a cada 16ms (~60 FPS) independente de qualquer coisa
            if (elapsed >= 16) {
                if (BitBlt(hdcMemDC, 0, 0, screenWidth, screenHeight, 
                        hdcScreen, 0, 0, SRCCOPY | CAPTUREBLT)) {
                    
                    if (GetDIBits(hdcScreen, hbmScreen, 0, screenHeight,
                                backBuffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
                        
                        {
                            std::lock_guard<std::mutex> lock(bufferMutex);
                            std::swap(frontBuffer, backBuffer);
                        }
                        
                        frameCount++;
                    }
                }
                
                lastCapture = now;
            }
            
            // Dormir apenas 1ms para manter responsividade
            Sleep(1);
        }
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

    // Red -> u (x-coord in slice)
    // Blue -> v (y-coord in slice)
    // Green -> slice index
    
    float u = (color.r * (lutSize - 1.0) + 0.5) / lutSize;
    float v = (color.b * (lutSize - 1.0) + 0.5) / lutSize;
    float slice = color.g * (lutSize - 1.0);

    float slice_floor = floor(slice);
    float slice_frac = slice - slice_floor;

    // The normalized WIDTH of a single 32x32 slice
    float slice_width = 1.0 / lutSize;

    // We calculate the X coordinate by finding the correct horizontal slice.
    // We use 'v' for the Y coordinate directly.
    vec2 uv0 = vec2( (u + slice_floor) * slice_width, v );
    vec2 uv1 = vec2( (u + slice_floor + 1.0) * slice_width, v );

    vec3 sample0 = texture(lut, uv0).rgb;
    vec3 sample1 = texture(lut, uv1).rgb;

    return mix(sample0, sample1, slice_frac);
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

    // vec3 corrected = applyLUT3D(color, lutTexture);
    // FragColor = vec4(corrected, 1.0);

    // FragColor = vec4(1.0, 0.0, 0.0, 0.5); 
}
)";

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ==================== OVERLAY FINAL ====================
class FinalOverlayFilter {
private:
    GLFWwindow* window;
    IndependentScreenCapture* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO;
    unsigned int screenTexture;
    
    std::atomic<bool> correctionEnabled;
    std::atomic<float> correctionStrength;
    std::atomic<bool> useLUT;
    
    HWND overlayHwnd;
    
    int lastFrameCount = 0;
    bool shouldClose = false;
    
public:
    FinalOverlayFilter() : correctionEnabled(false), correctionStrength(0.6f), useLUT(false) {
        g_filterInstance = this;
    }
    
    ~FinalOverlayFilter() {
        g_filterInstance = nullptr;
    }
    
    // Métodos públicos para hotkeys
    void toggleCorrection() {
        std::cout << "DEBUG: toggleCorrection() CALLED!" << std::endl;
        bool current = correctionEnabled.load();
        correctionEnabled.store(!current);
        std::cout << "Filtro " << (!current ? "✅ ATIVADO" : "❌ DESATIVADO") << std::endl;
    }
    
    void increaseIntensity() {
        float current = correctionStrength.load();
        correctionStrength.store(std::min(1.0f, current + 0.1f));
        std::cout << "Intensidade: " << (int)(correctionStrength.load() * 100) << "%" << std::endl;
    }
    
    void decreaseIntensity() {
        float current = correctionStrength.load();
        correctionStrength.store(std::max(0.0f, current - 0.1f));
        std::cout << "Intensidade: " << (int)(correctionStrength.load() * 100) << "%" << std::endl;
    }
    
    void toggleMethod() {
        if (lutLoader->getIsLoaded()) {
            bool current = useLUT.load();
            useLUT.store(!current);
            std::cout << "Método: " << (!current ? "LUT" : "Matemático") << std::endl;
        } else {
            std::cout << "⚠️ LUT não disponível" << std::endl;
        }
    }
    
    void requestClose() {
        shouldClose = true;
    }
    
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
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        
        window = glfwCreateWindow(screenWidth, screenHeight, "Daltonismo Filter", NULL, NULL);
        if (!window) {
            std::cerr << "Falha ao criar janela" << std::endl;
            glfwTerminate();
            return false;
        }
        
        glfwSetWindowPos(window, 0, 0);
        glfwMakeContextCurrent(window);
        
        overlayHwnd = glfwGetWin32Window(window);
        
        // Configurar janela overlay
        LONG_PTR exStyle = GetWindowLong(overlayHwnd, GWL_EXSTYLE);
        SetWindowLong(overlayHwnd, GWL_EXSTYLE, 
                     exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | 
                     WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);

        SetWindowDisplayAffinity(overlayHwnd, WDA_EXCLUDEFROMCAPTURE);
        
        SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
        
        // Substituir Window Procedure
        // originalWndProc = (WNDPROC)SetWindowLongPtr(overlayHwnd, GWLP_WNDPROC, (LONG_PTR)OverlayWndProc);
        g_originalWndProc = (WNDPROC)SetWindowLongPtr(overlayHwnd, GWLP_WNDPROC, (LONG_PTR)OverlayWndProc);

        // REGISTRAR HOTKEYS GLOBAIS
        if (!RegisterHotKey(overlayHwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D')) {
            std::cerr << "⚠️ Falha ao registrar hotkey Ctrl+Shift+D" << std::endl;
        }
        if (!RegisterHotKey(overlayHwnd, HOTKEY_INCREASE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_PLUS)) {
            std::cerr << "⚠️ Falha ao registrar hotkey Ctrl+Shift++" << std::endl;
        }
        if (!RegisterHotKey(overlayHwnd, HOTKEY_DECREASE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_MINUS)) {
            std::cerr << "⚠️ Falha ao registrar hotkey Ctrl+Shift+-" << std::endl;
        }
        if (!RegisterHotKey(overlayHwnd, HOTKEY_METHOD, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'L')) {
            std::cerr << "⚠️ Falha ao registrar hotkey Ctrl+Shift+L" << std::endl;
        }
        if (!RegisterHotKey(overlayHwnd, HOTKEY_QUIT, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Q')) {
            std::cerr << "⚠️ Falha ao registrar hotkey Ctrl+Shift+Q" << std::endl;
        }
        
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Falha ao inicializar GLAD" << std::endl;
            return false;
        }
        
        glfwSwapInterval(0);
        
        capture = new IndependentScreenCapture();
        if (!capture->initialize()) {
            std::cerr << "Falha ao inicializar captura" << std::endl;
            return false;
        }
        
        // capture->setOverlayWindow(overlayHwnd);
        // capture->setCorrectionState(&correctionEnabled);

        capture->start();
        
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
        std::cout << "║ ✅ HOTKEYS GLOBAIS REGISTRADOS        ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝" << std::endl;
        std::cout << "Funcionam de QUALQUER lugar!" << std::endl;
        std::cout << "\nControles GLOBAIS:" << std::endl;
        std::cout << "  Ctrl+Shift+D - Ativar/Desativar" << std::endl;
        std::cout << "  Ctrl+Shift++ - Aumentar intensidade" << std::endl;
        std::cout << "  Ctrl+Shift+- - Diminuir intensidade" << std::endl;
        std::cout << "  Ctrl+Shift+L - Alternar LUT/Matemático" << std::endl;
        std::cout << "  Ctrl+Shift+Q - Sair\n" << std::endl;
        
        return true;
    }
    
    void run() {
        auto lastFpsCheck = steady_clock::now();
        int renderFrames = 0;
        
        MSG msg;
        while (!shouldClose && !glfwWindowShouldClose(window)) {
            // std::cout << " Entrei no loop de renderiação " << std::endl;
            // CRITICAL: Processar mensagens do Windows (para hotkeys)
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    shouldClose = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            // std::cout << " Sai do loop de menssagem " << std::endl;
            
            // Poll GLFW events
            glfwPollEvents();
            
            bool enabled = correctionEnabled.load();
            // std::cout << (enabled ? "Ativado aqui" : "Desativado aqui") << " valor em enabled: " << enabled << std::endl; 
            
            if (enabled) {
                // std::cout << "LOOP: Filter is enabled. Entering render block." << std::endl;
                updateScreenTexture();
                // std::cout << "LOOP: AFTER updateScreenTexture()." << std::endl;
                render();
                // std::cout << "LOOP: AFTER render()." << std::endl;
                SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
            } else {
                SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            
            glfwSwapBuffers(window);
            renderFrames++;
            
            // Debug FPS
            auto now = steady_clock::now();
            auto elapsed = duration_cast<seconds>(now - lastFpsCheck).count();
            if (elapsed >= 5) {
                int captureFrames = capture->getFrameCount();
                std::cout << "📊 Render: " << (renderFrames / 5) << " FPS | ";
                std::cout << "Capture: " << ((captureFrames - lastFrameCount) / 5) << " FPS | ";
                std::cout << "Filtro: " << (enabled ? "ON" : "OFF") << std::endl;
                lastFrameCount = captureFrames;
                renderFrames = 0;
                lastFpsCheck = now;
            }
            
            // Pequeno sleep para não usar 100% CPU
            Sleep(1);
            // std::cout << "LOOP: End of cycle." << std::endl;
        }
    }
    
    void cleanup() {
        // Desregistrar hotkeys
        UnregisterHotKey(overlayHwnd, HOTKEY_TOGGLE);
        UnregisterHotKey(overlayHwnd, HOTKEY_INCREASE);
        UnregisterHotKey(overlayHwnd, HOTKEY_DECREASE);
        UnregisterHotKey(overlayHwnd, HOTKEY_METHOD);
        UnregisterHotKey(overlayHwnd, HOTKEY_QUIT);
        
        capture->stop();
        delete capture;
        delete shader;
        delete lutLoader;
        
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteTextures(1, &screenTexture);
        
        glfwTerminate();
    }
    
    // Remover keyCallback antigo - não é mais necessário
    
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
        shader->setBool("enableCorrection", correctionEnabled.load());
        shader->setFloat("correctionStrength", correctionStrength.load());
        shader->setBool("useLUT", useLUT.load());
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        
        if (useLUT.load() && lutLoader->getIsLoaded()) {
            lutLoader->bindLUT(1);
        }
        
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};

// ==================== WINDOW PROCEDURE COM HOTKEYS GLOBAIS ====================

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            if (g_filterInstance) {
                switch (wParam) {
                    case HOTKEY_TOGGLE:
                        g_filterInstance->toggleCorrection();
                        break;
                    case HOTKEY_INCREASE:
                        g_filterInstance->increaseIntensity();
                        break;
                    case HOTKEY_DECREASE:
                        g_filterInstance->decreaseIntensity();
                        break;
                    case HOTKEY_METHOD:
                        g_filterInstance->toggleMethod();
                        break;
                    case HOTKEY_QUIT:
                        PostQuitMessage(0);
                        break;
                }
            }
            return 0;
            
        case WM_PAINT:
            ValidateRect(hwnd, NULL); 
            return 0;
            
        case WM_ERASEBKGND:
            return 1;
            
        case WM_NCPAINT:
        case WM_NCCALCSIZE:
            return 0;
    }
    
    // return DefWindowProc(hwnd, msg, wParam, lParam);
    return CallWindowProc(g_originalWndProc, hwnd, msg, wParam, lParam);
}

// ==================== MAIN ====================

int main() {
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Filtro Daltonismo                     ║" << std::endl;
    std::cout << "║  HOTKEYS GLOBAIS                       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    
    FinalOverlayFilter filter;
    
    if (!filter.initialize()) {
        std::cerr << "\n❌ Falha ao inicializar!" << std::endl;
        std::cerr << "Pressione Enter para sair..." << std::endl;
        std::cin.get();
        return -1;
    }
    
    std::cout << "✅ PRONTO! Pressione Ctrl+Shift+D de QUALQUER lugar!" << std::endl;
    std::cout << "   Hotkeys funcionam GLOBALMENTE.\n" << std::endl;
    
    filter.run();
    filter.cleanup();
    
    std::cout << "\n✅ Filtro encerrado!" << std::endl;
    
    return 0;
}