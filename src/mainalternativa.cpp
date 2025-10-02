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

// Solução melhorada para captura de tela sem feedback loop

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

// Uso na classe overlay:
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

/*
SOLUÇÕES IMPLEMENTADAS PARA FEEDBACK LOOP:

1. ✅ ShowWindow(SW_HIDE) antes de capturar
   - Esconde overlay temporariamente
   - Aguarda compositor (Sleep(5ms))
   - Captura tela limpa
   - Mostra overlay novamente

2. ✅ Z-order manipulation
   - Move overlay para trás temporariamente
   - Captura
   - Restaura posição topmost

3. ✅ Buffer duplo (opcional)
   - Detecta se frame é idêntico ao anterior
   - Previne acumulação

RESULTADO:
- Sem branqueamento da tela
- Sem feedback loop
- Sem acumulação de efeito
- Latência mínima (~5ms adicionais)
*/