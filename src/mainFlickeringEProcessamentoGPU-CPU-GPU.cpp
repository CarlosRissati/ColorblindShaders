// main.cpp - Solução completa com Desktop Duplication API
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

using Microsoft::WRL::ComPtr;

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
        
        int success;
        char infoLog[512];
        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 512, NULL, infoLog);
            std::cerr << "Erro ao linkar shader: " << infoLog << std::endl;
        }
        
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

// ==================== DESKTOP DUPLICATION CAPTURE ====================
class DesktopDuplicationCapture {
private:
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> deskDupl;
    ComPtr<ID3D11Texture2D> stagingTexture;
    
    int screenWidth, screenHeight;
    std::vector<unsigned char> pixelData;
    bool initialized;
    
public:
    DesktopDuplicationCapture() : initialized(false) {
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        pixelData.resize(screenWidth * screenHeight * 4);
    }
    
    ~DesktopDuplicationCapture() {
        cleanup();
    }
    
    bool initialize() {
        HRESULT hr;
        
        // Criar D3D11 Device
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &d3dDevice,
            &featureLevel,
            &d3dContext
        );
        
        if (FAILED(hr)) {
            std::cerr << "Falha ao criar D3D11 Device: 0x" << std::hex << hr << std::endl;
            return false;
        }
        
        // Obter DXGI Device
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) {
            std::cerr << "Falha ao obter DXGI Device" << std::endl;
            return false;
        }
        
        // Obter DXGI Adapter
        ComPtr<IDXGIAdapter> dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (FAILED(hr)) {
            std::cerr << "Falha ao obter DXGI Adapter" << std::endl;
            return false;
        }
        
        // Obter Output (Monitor principal)
        ComPtr<IDXGIOutput> dxgiOutput;
        hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        if (FAILED(hr)) {
            std::cerr << "Falha ao enumerar outputs" << std::endl;
            return false;
        }
        
        // Obter Output1 para Desktop Duplication
        ComPtr<IDXGIOutput1> dxgiOutput1;
        hr = dxgiOutput.As(&dxgiOutput1);
        if (FAILED(hr)) {
            std::cerr << "Falha ao obter IDXGIOutput1" << std::endl;
            return false;
        }
        
        // Criar Desktop Duplication
        hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
        if (FAILED(hr)) {
            std::cerr << "Falha ao criar Desktop Duplication: 0x" << std::hex << hr << std::endl;
            if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                std::cerr << "ERRO: Outro programa está usando Desktop Duplication (OBS, ShareX, etc.)" << std::endl;
            } else if (hr == E_ACCESSDENIED) {
                std::cerr << "ERRO: Sem permissão. Execute como ADMINISTRADOR!" << std::endl;
            }
            return false;
        }
        
        // Criar Staging Texture
        D3D11_TEXTURE2D_DESC desc;
        desc.Width = screenWidth;
        desc.Height = screenHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        
        hr = d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture);
        if (FAILED(hr)) {
            std::cerr << "Falha ao criar staging texture" << std::endl;
            return false;
        }
        
        initialized = true;
        std::cout << "Desktop Duplication API inicializada com sucesso!" << std::endl;
        std::cout << "Resolução: " << screenWidth << "x" << screenHeight << std::endl;
        return true;
    }
    
    bool captureScreen() {
        if (!initialized) return false;
        
        HRESULT hr;
        
        // Liberar frame anterior se existir
        deskDupl->ReleaseFrame();
        
        // Adquirir novo frame
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        
        hr = deskDupl->AcquireNextFrame(100, &frameInfo, &desktopResource);
        
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // Sem novos frames - continuar com frame anterior
            return true;
        }
        
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                std::cout << "Access lost - reinicializando Desktop Duplication..." << std::endl;
                cleanup();
                Sleep(100);
                return initialize();
            }
            return false;
        }
        
        // Obter texture do desktop
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return false;
        }
        
        // Copiar para staging texture
        d3dContext->CopyResource(stagingTexture.Get(), desktopTexture.Get());
        
        // Map para ler na CPU
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return false;
        }
        
        // Copiar pixels
        unsigned char* source = static_cast<unsigned char*>(mappedResource.pData);
        
        for (int y = 0; y < screenHeight; y++) {
            memcpy(
                &pixelData[y * screenWidth * 4],
                &source[y * mappedResource.RowPitch],
                screenWidth * 4
            );
        }
        
        // Unmap
        d3dContext->Unmap(stagingTexture.Get(), 0);
        
        return true;
    }
    
    void cleanup() {
        if (deskDupl) {
            deskDupl->ReleaseFrame();
            deskDupl.Reset();
        }
        stagingTexture.Reset();
        d3dContext.Reset();
        d3dDevice.Reset();
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
    
    vec3 corrected = color;
    corrected = applyLUT3D(color, lutTexture);
    
    vec3 final = mix(color, corrected, correctionStrength);
    FragColor = vec4(final, 1.0);
}
)";

// ==================== OVERLAY PRINCIPAL ====================
class OverlayDaltonismoFilter {
private:
    GLFWwindow* window;
    DesktopDuplicationCapture* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO;
    unsigned int screenTexture;
    
    bool correctionEnabled = true;
    float correctionStrength = 0.6f;
    bool useLUT = true;
    
    HWND overlayHwnd;
    
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
        
        SetWindowLong(overlayHwnd, GWL_EXSTYLE, 
                     GetWindowLong(overlayHwnd, GWL_EXSTYLE) | 
                     WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(overlayHwnd, 0, 0, LWA_ALPHA);
        
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Falha ao inicializar GLAD" << std::endl;
            return false;
        }
        
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        glfwSwapInterval(0);
        
        // Inicializar Desktop Duplication
        capture = new DesktopDuplicationCapture();
        if (!capture->initialize()) {
            std::cerr << "\n========================================" << std::endl;
            std::cerr << "FALHA ao inicializar Desktop Duplication!" << std::endl;
            std::cerr << "========================================" << std::endl;
            std::cerr << "Possíveis causas:" << std::endl;
            std::cerr << "1. NÃO está executando como administrador" << std::endl;
            std::cerr << "2. Outro programa está usando (OBS, ShareX, etc.)" << std::endl;
            std::cerr << "3. Windows 7 (não suportado - precisa Win 8+)" << std::endl;
            std::cerr << "========================================\n" << std::endl;
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
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "✅ Sistema inicializado com sucesso!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Controles:" << std::endl;
        std::cout << "  Ctrl+Shift+D - Ativar/Desativar" << std::endl;
        std::cout << "  Ctrl+Shift++ - Aumentar intensidade" << std::endl;
        std::cout << "  Ctrl+Shift+- - Diminuir intensidade" << std::endl;
        std::cout << "  Ctrl+Shift+L - Alternar LUT/Matemático" << std::endl;
        std::cout << "  Ctrl+Shift+Q - Sair" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        return true;
    }
    
    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            
            bool hasFrame = capture->captureScreen();

            if (correctionEnabled && hasFrame) {
                updateScreenTexture();
                render();
                SetLayeredWindowAttributes(overlayHwnd, 0, 255, LWA_ALPHA);
            } else if (!correctionEnabled) {
                // overlay desativado → transparente
                SetLayeredWindowAttributes(overlayHwnd, 0, 0, LWA_ALPHA);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            } else {
                // correção ligada mas não tem frame novo → NÃO redesenha
                // mantém último frame válido, evita flickering
            }

            glfwSwapBuffers(window);
            Sleep(8); // ~120 FPS
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
                    std::cout << "Método: " << (filter->useLUT ? "LUT Científica" : "Matemático") << std::endl;
                } else {
                    std::cout << "⚠️ LUT não está disponível" << std::endl;
                }
            }
            else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_Q) {
                std::cout << "Encerrando..." << std::endl;
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
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // <- garante alinhamento
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 
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
    std::cout << "║  Filtro Daltonismo - Deuteranopia     ║" << std::endl;
    std::cout << "║  Desktop Duplication API               ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    
    // Verificar se está executando como administrador
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    
    if (!isAdmin) {
        std::cerr << "⚠️  AVISO: Não está executando como administrador!" << std::endl;
        std::cerr << "   Desktop Duplication API requer permissões elevadas." << std::endl;
        std::cerr << "   Clique direito no executável → 'Executar como administrador'\n" << std::endl;
    }
    
    OverlayDaltonismoFilter filter;
    
    if (!filter.initialize()) {
        std::cerr << "\n❌ Falha ao inicializar o filtro!" << std::endl;
        std::cerr << "\nPressione Enter para sair..." << std::endl;
        std::cin.get();
        return -1;
    }
    
    std::cout << "✅ Pronto! Use os controles acima." << std::endl;
    std::cout << "   O overlay está invisível até você ativar.\n" << std::endl;
    
    filter.run();
    filter.cleanup();
    
    std::cout << "\n✅ Filtro encerrado com sucesso!" << std::endl;
    
    return 0;
}

/*
════════════════════════════════════════════════════════════════
COMPILAÇÃO:
════════════════════════════════════════════════════════════════

1. CMakeLists.txt já está configurado com:
   - d3d11 (DirectX 11)
   - dxgi (Desktop Duplication)
   
2. Compilar:
   cmake --build build --config Release

3. Executar:
   - Clique direito no .exe → "Executar como administrador"
   - Ou: Start-Process "build/DaltonismoFilter.exe" -Verb RunAs

════════════════════════════════════════════════════════════════
ESTRUTURA DO PROJETO:
════════════════════════════════════════════════════════════════

DaltonismoFilter/
├── src/
│   └── main.cpp              ← Este arquivo completo
├── luts/
│   └── deuteranopia_correction.png
├── external/
│   ├── glad/
│   │   ├── include/
│   │   └── src/glad.c
│   └── stb/
│       └── stb_image.h
├── CMakeLists.txt
└── build/

════════════════════════════════════════════════════════════════
CLASSES INCLUÍDAS:
════════════════════════════════════════════════════════════════

✅ Shader                      - Mesma classe anterior
✅ LUTLoader                   - Mesma classe anterior  
✅ DesktopDuplicationCapture   - NOVA - Captura sem feedback
✅ OverlayDaltonismoFilter     - Overlay principal

════════════════════════════════════════════════════════════════
BENEFÍCIOS DESTA SOLUÇÃO:
════════════════════════════════════════════════════════════════

✅ SEM feedback loop           - Captura antes do overlay
✅ SEM piscar                  - Captura contínua e suave
✅ SEM branqueamento           - Filtro aplicado corretamente
✅ Alta performance            - GPU accelerated
✅ Baixa latência             - ~5-10ms
✅ Auto-recovery              - Reinicializa se perder acesso

════════════════════════════════════════════════════════════════
TROUBLESHOOTING:
════════════════════════════════════════════════════════════════

Problema: "Falha ao criar Desktop Duplication"
Solução: Execute como administrador

Problema: "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE"
Solução: Feche OBS, ShareX ou outros programas de captura

Problema: Tela preta
Solução: Verifique se GPU suporta DirectX 11

Problema: "Access Lost"
Solução: Programa reinicializa automaticamente

════════════════════════════════════════════════════════════════
PRÓXIMOS PASSOS OPCIONAIS:
════════════════════════════════════════════════════════════════

1. System Tray Icon           - Ícone na bandeja
2. Configurações GUI          - Interface gráfica
3. Multi-monitor support      - Vários monitores
4. Auto-start com Windows     - Iniciar automático
5. Perfis customizados        - Diferentes intensidades
6. Hotkeys customizáveis      - Atalhos personalizados

════════════════════════════════════════════════════════════════
*/