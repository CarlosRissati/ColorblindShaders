#version 330 core

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D screenTexture;
uniform bool enableCorrection;
uniform float correctionStrength;

// Correção CIENTÍFICA para Deuteranopia baseada em pesquisa
vec3 correctDeuteranopia(vec3 color) {
    // Transformação RGB para espaço LMS (Long, Medium, Short wavelengths)
    mat3 rgbToLms = mat3(
        0.31399022, 0.63951294, 0.04649755,
        0.15537241, 0.75789446, 0.08670142,
        0.01775239, 0.10944209, 0.87256922
    );
    
    // Transformação LMS de volta para RGB
    mat3 lmsToRgb = mat3(
         5.47221206, -4.6419601,  0.16963708,
        -1.1252419,   2.29317094, -0.1678952,
         0.02980165, -0.19318073,  1.16364789
    );
    
    // Converter RGB para LMS
    vec3 lms = rgbToLms * color;
    
    // DEUTERANOPIA: Perda do canal M (médio)
    // Algoritmo baseado em Machado, Oliveira e Fernandes (2009)
    
    // Simulação da deuteranopia (para referência - não usada diretamente)
    // lms.g = 0.494207 * lms.r + 1.24827 * lms.b;
    
    // CORREÇÃO: Redistribuir informação para melhorar discriminação
    vec3 correctedLms = lms;
    
    // Método 1: Realçar diferenças Red-Green
    float redGreenDiff = lms.r - lms.g;
    correctedLms.r = lms.r + 0.7 * redGreenDiff;
    correctedLms.g = lms.g - 0.7 * redGreenDiff;
    
    // Método 2: Mapear informação perdida para canal azul
    correctedLms.b = lms.b + 0.3 * abs(redGreenDiff);
    
    // Converter de volta para RGB
    vec3 correctedRgb = lmsToRgb * correctedLms;
    
    // Garantir valores válidos
    return clamp(correctedRgb, 0.0, 1.0);
}

// Correção alternativa: Método Daltonize
vec3 daltonizeDeuteranopia(vec3 color) {
    // Simular como pessoa com deuteranopia vê
    mat3 deuteranopia_sim = mat3(
        0.625, 0.375, 0.0,
        0.7,   0.3,   0.0,
        0.0,   0.3,   0.7
    );
    
    vec3 simulated = deuteranopia_sim * color;
    vec3 error = color - simulated;
    
    // Adicionar erro aos canais que podem ser percebidos
    vec3 corrected = color;
    corrected.r += error.r * 0.0;
    corrected.g += error.g * 0.0; 
    corrected.b += error.r * 0.7 + error.g * 0.7;
    
    return clamp(corrected, 0.0, 1.0);
}

// Correção híbrida otimizada
vec3 hybridDeuteranopiaCorrection(vec3 color) {
    // Preservar luminância original
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    
    // Aplicar correção baseada em contraste Red-Green
    float redGreenRatio = color.r / max(color.g, 0.001);
    
    vec3 corrected = color;
    
    if (redGreenRatio > 1.2) {
        // Área mais vermelha - realçar diferença
        corrected.r = min(1.0, color.r * 1.2);
        corrected.b = min(1.0, color.b + (color.r - color.g) * 0.4);
    } else if (redGreenRatio < 0.8) {
        // Área mais verde - realçar diferença  
        corrected.g = min(1.0, color.g * 1.1);
        corrected.b = min(1.0, color.b + (color.g - color.r) * 0.3);
    }
    
    // Preservar luminância aproximada
    float newLuminance = dot(corrected, vec3(0.299, 0.587, 0.114));
    if (newLuminance > 0.001) {
        corrected *= luminance / newLuminance;
    }
    
    return clamp(corrected, 0.0, 1.0);
}

void main()
{
    vec3 originalColor = texture(screenTexture, TexCoord).rgb;
    
    if (!enableCorrection) {
        FragColor = vec4(originalColor, 1.0);
        return;
    }
    
    // Escolher método de correção (teste diferentes)
    vec3 correctedColor;
    
    // MÉTODO 1: Científico LMS (mais preciso)
    // correctedColor = correctDeuteranopia(originalColor);
    
    // MÉTODO 2: Daltonize (mais suave)
    // correctedColor = daltonizeDeuteranopia(originalColor);
    
    // MÉTODO 3: Híbrido (recomendado)
    correctedColor = hybridDeuteranopiaCorrection(originalColor);
    
    // Misturar com original baseado na intensidade
    vec3 finalColor = mix(originalColor, correctedColor, correctionStrength);
    
    FragColor = vec4(finalColor, 1.0);
}

/*
TESTANDO OS MÉTODOS:
1. Descomente um método por vez no main()
2. Teste com diferentes valores de correctionStrength
3. Use imagens de teste para daltonismo (Ishihara plates)

CONTROLES SUGERIDOS:
- ESPAÇO: Liga/desliga correção
- 1,2,3: Alternar entre métodos de correção
- +/-: Ajustar intensidade
*/