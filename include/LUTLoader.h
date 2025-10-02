#ifndef LUT_LOADER_H
#define LUT_LOADER_H

#include <iostream>
#include <string>
#include <glad/glad.h>

// Para carregar PNG - você pode usar stb_image ou SOIL
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>

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
    
    // Carregar LUT do arquivo PNG
    bool loadLUT(const std::string& filepath) {
        std::cout << "Carregando LUT: " << filepath << std::endl;
        
        // Carregar imagem PNG
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
        
        if (!data) {
            std::cerr << "Erro ao carregar LUT: " << stbi_failure_reason() << std::endl;
            return false;
        }
        
        // Verificar dimensões (deve ser 1024x32 ou 32x1024)
        if (!((width == 1024 && height == 32) || (width == 32 && height == 1024))) {
            std::cerr << "Erro: LUT deve ter dimensões 1024x32 ou 32x1024. Atual: " 
                      << width << "x" << height << std::endl;
            stbi_image_free(data);
            return false;
        }
        
        std::cout << "LUT carregada: " << width << "x" << height << " (" << channels << " canais)" << std::endl;
        
        // Gerar textura OpenGL
        glGenTextures(1, &lutTextureID);
        glBindTexture(GL_TEXTURE_2D, lutTextureID);
        
        // Configurar parâmetros da textura (MUITO IMPORTANTE para LUTs)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // Determinar formato interno baseado nos canais
        GLenum format;
        GLenum internalFormat;
        
        switch (channels) {
            case 1:
                format = GL_RED;
                internalFormat = GL_R8;
                break;
            case 3:
                format = GL_RGB;
                internalFormat = GL_RGB8;
                break;
            case 4:
                format = GL_RGBA;
                internalFormat = GL_RGBA8;
                break;
            default:
                std::cerr << "Formato de canal não suportado: " << channels << std::endl;
                stbi_image_free(data);
                return false;
        }
        
        // Upload dos dados para GPU
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        
        // Verificar erros OpenGL
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            std::cerr << "Erro OpenGL ao criar textura LUT: " << error << std::endl;
            stbi_image_free(data);
            return false;
        }
        
        // Limpar dados da CPU
        stbi_image_free(data);
        
        isLoaded = true;
        std::cout << "LUT carregada com sucesso! Texture ID: " << lutTextureID << std::endl;
        
        return true;
    }
    
    // Gerar LUT de teste procedural (para desenvolvimento)
    bool generateTestLUT() {
        std::cout << "Gerando LUT de teste 32x1024..." << std::endl;
        
        width = 32;
        height = 1024;
        channels = 3;
        
        // Alocar dados para LUT de teste
        std::vector<unsigned char> lutData(width * height * channels);
        
        // Gerar LUT 3D no formato 32x1024
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int pixelIndex = (y * width + x) * channels;
                
                // Mapear coordenadas para RGB 32x32x32
                int slice = x;  // Blue slice (0-31)
                int green = y / 32;  // Green (0-31)
                int red = y % 32;    // Red (0-31)
                
                // Normalizar para [0-255]
                float r = red / 31.0f;
                float g = green / 31.0f;
                float b = slice / 31.0f;
                
                // Aplicar correção de teste para deuteranopia
                // (isso é só para teste - use LUTs reais para produção)
                if (r > g && r > 0.3f) {
                    b = std::min(1.0f, b + (r - g) * 0.3f);
                }
                
                lutData[pixelIndex] = (unsigned char)(r * 255);
                lutData[pixelIndex + 1] = (unsigned char)(g * 255);
                lutData[pixelIndex + 2] = (unsigned char)(b * 255);
            }
        }
        
        // Criar textura OpenGL
        glGenTextures(1, &lutTextureID);
        glBindTexture(GL_TEXTURE_2D, lutTextureID);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, lutData.data());
        
        isLoaded = true;
        std::cout << "LUT de teste gerada com sucesso!" << std::endl;
        
        return true;
    }
    
    // Bind da textura LUT
    void bindLUT(int textureUnit = 1) {
        if (isLoaded) {
            glActiveTexture(GL_TEXTURE0 + textureUnit);
            glBindTexture(GL_TEXTURE_2D, lutTextureID);
        }
    }
    
    // Getters
    bool getIsLoaded() const { return isLoaded; }
    unsigned int getTextureID() const { return lutTextureID; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    
    // Salvar LUT de teste como PNG (para debug)
    bool saveLUTAsPNG(const std::string& filepath) {
        if (!isLoaded) return false;
        
        // Ler dados da GPU de volta
        std::vector<unsigned char> data(width * height * channels);
        glBindTexture(GL_TEXTURE_2D, lutTextureID);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
        
        // Salvar como PNG usando stb_image_write
        #define STB_IMAGE_WRITE_IMPLEMENTATION
        #include "stb_image_write.h"
        
        return stbi_write_png(filepath.c_str(), width, height, channels, data.data(), width * channels);
    }
};

#endif // LUT_LOADER_H