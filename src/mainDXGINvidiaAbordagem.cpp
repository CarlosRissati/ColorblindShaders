// main.cpp - Solução de alta performance com D3D11-OpenGL Interop (CORRIGIDO)
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#define NOMINMAX
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using Microsoft::WRL::ComPtr;

#define WGL_ACCESS_READ_ONLY_NV 0x0000

// ==================== NOVO: PONTEIROS PARA FUNÇÕES DA EXTENSÃO WGL_NV_DX_interop ====================
typedef HANDLE(WINAPI* PFNWGLDXOPENDEVICENVPROC) (void* dxDevice);
typedef BOOL(WINAPI* PFNWGLDXCLOSEDEVICENVPROC) (HANDLE hDevice);
typedef HANDLE(WINAPI* PFNWGLDXREGISTEROBJECTNVPROC) (HANDLE hDevice, void* dxObject, GLuint name, GLenum type, GLenum access);
typedef BOOL(WINAPI* PFNWGLDXUNREGISTEROBJECTNVPROC) (HANDLE hDevice, HANDLE hObject);
typedef BOOL(WINAPI* PFNWGLDXLOCKOBJECTSNVPROC) (HANDLE hDevice, GLint count, HANDLE* hObjects);
typedef BOOL(WINAPI* PFNWGLDXUNLOCKOBJECTSNVPROC) (HANDLE hDevice, GLint count, HANDLE* hObjects);

PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = nullptr;
PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = nullptr;
PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = nullptr;
PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = nullptr;
PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = nullptr;
PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = nullptr;

// Função para carregar os ponteiros
bool loadDxInteropFunctions() {
    wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
    wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)wglGetProcAddress("wglDXCloseDeviceNV");
    wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");
    wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
    wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXLockObjectsNV");
    wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXUnlockObjectsNV");

    if (!wglDXOpenDeviceNV || !wglDXCloseDeviceNV || !wglDXRegisterObjectNV || !wglDXUnregisterObjectNV || !wglDXLockObjectsNV || !wglDXUnlockObjectsNV) {
        std::cerr << "Erro: Nao foi possivel carregar as funcoes da extensao WGL_NV_DX_interop. Sua GPU suporta essa extensao?" << std::endl;
        return false;
    }
    return true;
}

// ==================== CLASSE SHADER (SEM ALTERAÇÕES) ====================
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
    void setBool(const std::string& name, bool value) const { glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value); }
    void setFloat(const std::string& name, float value) const { glUniform1f(glGetUniformLocation(ID, name.c_str()), value); }
    void setInt(const std::string& name, int value) const { glUniform1i(glGetUniformLocation(ID, name.c_str()), value); }
};

// ==================== CLASSE LUT LOADER (SEM ALTERAÇÕES) ====================
class LUTLoader {
private:
    unsigned int lutTextureID;
    bool isLoaded;
public:
    LUTLoader() : lutTextureID(0), isLoaded(false) {}
    ~LUTLoader() { if (isLoaded) glDeleteTextures(1, &lutTextureID); }

    bool loadLUT(const std::string& filepath) {
        int width, height, channels;
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
        if (!data) {
            std::cerr << "Erro ao carregar LUT: " << stbi_failure_reason() << std::endl;
            return false;
        }
        glGenTextures(1, &lutTextureID);
        glBindTexture(GL_TEXTURE_2D, lutTextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
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

// ==================== DESKTOP DUPLICATION (SEM ALTERAÇÕES DA VERSÃO ANTERIOR) ====================
class DesktopDuplicationCapture {
private:
    ComPtr<IDXGIOutputDuplication> deskDupl;
    bool initialized;

public:
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<ID3D11Texture2D> sharedTexture;
    HANDLE sharedTextureHandle;
    int screenWidth, screenHeight;

    DesktopDuplicationCapture() : initialized(false), sharedTextureHandle(nullptr) {
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
    }

    ~DesktopDuplicationCapture() { cleanup(); }

    bool initialize() {
        // ... (nenhuma alteração nesta função)
        HRESULT hr;
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIAdapter> dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIOutput> dxgiOutput;
        hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIOutput1> dxgiOutput1;
        hr = dxgiOutput.As(&dxgiOutput1);
        if (FAILED(hr)) return false;

        hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = screenWidth;
        desc.Height = screenHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        hr = d3dDevice->CreateTexture2D(&desc, nullptr, &sharedTexture);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIResource> tempResource;
        hr = sharedTexture.As(&tempResource);
        if (FAILED(hr)) return false;
        
        hr = tempResource->GetSharedHandle(&sharedTextureHandle);
        if (FAILED(hr)) return false;

        initialized = true;
        return true;
    }

    // ALTERAÇÃO PRINCIPAL ESTÁ AQUI
    bool captureScreen() {
        if (!initialized) return false;

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;

        // 1. Libera o frame ANTERIOR (se houver um)
        deskDupl->ReleaseFrame(); 

        // 2. Tenta adquirir o PRÓXIMO frame
        // Aumentei um pouco o timeout para evitar perdas de frame em sistemas mais lentos
        HRESULT hr = deskDupl->AcquireNextFrame(10, &frameInfo, &desktopResource); 

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return true; // Não há um novo frame, mas não é um erro. Apenas renderizamos o anterior.
        }
        
        if (FAILED(hr)) {
            // Se o acesso for perdido (ex: mudança de modo de exibição), tenta reinicializar
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                std::cout << "Acesso ao dispositivo perdido. Tentando reinicializar..." << std::endl;
                cleanup();
                Sleep(100); 
                return initialize();
            }
            return false; // Outro erro
        }
        
        // 3. Se um novo frame foi adquirido com sucesso, copia para a textura compartilhada
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if(SUCCEEDED(hr)) {
            d3dContext->CopyResource(sharedTexture.Get(), desktopTexture.Get());
            d3dContext->Flush();
        }
        
        // O frame adquirido nesta chamada será liberado no INÍCIO da próxima chamada
        return true;
    }

    void cleanup() {
        // ... (nenhuma alteração nesta função)
        if (deskDupl) deskDupl->ReleaseFrame();
        deskDupl.Reset();
        sharedTexture.Reset();
        d3dContext.Reset();
        d3dDevice.Reset();
        initialized = false;
    }
};

// ==================== SHADERS (SEM ALTERAÇÕES) ====================
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
uniform float correctionStrength;

// Sua função applyLUT3D original, ela é mais robusta
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

void main() {
    vec3 color = texture(screenTexture, TexCoord).rgb;
    vec3 corrected_color = applyLUT3D(color, lutTexture);
    FragColor = vec4(mix(color, corrected_color, correctionStrength), 1.0);
}
)";

// ==================== OVERLAY PRINCIPAL (MODIFICADO) ====================
class OverlayDaltonismoFilter {
private:
    GLFWwindow* window;
    DesktopDuplicationCapture* capture;
    Shader* shader;
    LUTLoader* lutLoader;
    
    unsigned int VAO, VBO, screenTexture;
    bool correctionEnabled = true;
    float correctionStrength = 0.6f;
    HWND overlayHwnd;

    HANDLE dxDeviceHandle;
    HANDLE dxObjectHandle;

public:
    bool initialize() {
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
        
        capture = new DesktopDuplicationCapture();
        window = glfwCreateWindow(capture->screenWidth, capture->screenHeight, "Daltonismo Filter", NULL, NULL);
        if (!window) return false;

        glfwSetWindowPos(window, 0, 0);
        glfwMakeContextCurrent(window);
        gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

        // NOVO: Carregar as funções de interop DEPOIS de criar o contexto GL
        if (!loadDxInteropFunctions()) {
            return false;
        }

        overlayHwnd = glfwGetWin32Window(window);
        SetWindowLong(overlayHwnd, GWL_EXSTYLE, GetWindowLong(overlayHwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(overlayHwnd, 0, 0, LWA_ALPHA);
        
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCallback);
        glfwSwapInterval(0);
        
        if (!capture->initialize()) return false;
        
        setupGeometry();
        setupTexture();
        
        // Mover a inicialização da interop para depois do setup das texturas
        if (!initializeInterop()) return false;
        
        shader = new Shader(vertexShaderSource, fragmentShaderSource);
        lutLoader = new LUTLoader();
        if (!lutLoader->loadLUT("luts/deuteranopia_correction.png")) {
            std::cout << "LUT não encontrada, o filtro pode não funcionar." << std::endl;
        }
        
        return true;
    }

    bool initializeInterop() {
        dxDeviceHandle = wglDXOpenDeviceNV(capture->d3dDevice.Get());
        if (!dxDeviceHandle) return false;

        dxObjectHandle = wglDXRegisterObjectNV(dxDeviceHandle, capture->sharedTexture.Get(), screenTexture, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
        if (!dxObjectHandle) return false;
        
        return true;
    }
    
    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            
            if (correctionEnabled) {
                SetLayeredWindowAttributes(overlayHwnd, 0, 255, LWA_ALPHA);
                if (capture->captureScreen()) {
                     render();
                }
            } else {
                SetLayeredWindowAttributes(overlayHwnd, 0, 0, LWA_ALPHA);
            }
            glfwSwapBuffers(window);
        }
    }
    
    void cleanup() {
        if (dxDeviceHandle && dxObjectHandle) {
            wglDXUnregisterObjectNV(dxDeviceHandle, dxObjectHandle);
        }
        if (dxDeviceHandle) {
            wglDXCloseDeviceNV(dxDeviceHandle);
        }

        delete capture;
        delete shader;
        delete lutLoader;
        
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteTextures(1, &screenTexture);
        glfwTerminate();
    }
    
    static void keyCallback(GLFWwindow* window, int key, int, int action, int mods) {
        OverlayDaltonismoFilter* filter = (OverlayDaltonismoFilter*)glfwGetWindowUserPointer(window);
        if (action == GLFW_PRESS && mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) {
            if (key == GLFW_KEY_D) filter->correctionEnabled = !filter->correctionEnabled;
            if (key == GLFW_KEY_EQUAL) filter->correctionStrength = std::min(1.0f, filter->correctionStrength + 0.1f);
            if (key == GLFW_KEY_MINUS) filter->correctionStrength = std::max(0.0f, filter->correctionStrength - 0.1f);
            if (key == GLFW_KEY_Q) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

private:
    void setupGeometry() {
        float quadVertices[] = {
            -1.0f,  1.0f,  0.0f, 1.0f, -1.0f, -1.0f,  0.0f, 0.0f,  1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,  1.0f, -1.0f,  1.0f, 0.0f,  1.0f,  1.0f,  1.0f, 1.0f
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
    }
    
    void render() {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        wglDXLockObjectsNV(dxDeviceHandle, 1, &dxObjectHandle);

        shader->use();
        // NOVO: removido o setInt("enableCorrection"), pois o shader não usa mais
        shader->setInt("screenTexture", 0);
        shader->setInt("lutTexture", 1);
        shader->setFloat("correctionStrength", correctionStrength);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        
        if (lutLoader->getIsLoaded()) {
            lutLoader->bindLUT(1);
        }
        
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        wglDXUnlockObjectsNV(dxDeviceHandle, 1, &dxObjectHandle);
    }
};

// ==================== MAIN (SEM ALTERAÇÕES) ====================
int main() {
    OverlayDaltonismoFilter filter;
    if (!filter.initialize()) {
        std::cerr << "Falha ao inicializar o filtro!" << std::endl;
        std::cin.get();
        return -1;
    }
    filter.run();
    filter.cleanup();
    return 0;
}