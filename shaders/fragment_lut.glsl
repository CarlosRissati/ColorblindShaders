#version 330 core

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D screenTexture;    // Textura da captura de tela
uniform sampler2D lutTexture;       // LUT 32x1024 PNG
uniform bool enableCorrection;
uniform float correctionStrength;
uniform bool useLUT;                // Alternar entre LUT e correção matemática

// Função para aplicar LUT 3D usando textura 2D 32x1024
vec3 applyLUT3D(vec3 color, sampler2D lut) {
    // Normalizar cor de entrada [0,1]
    color = clamp(color, 0.0, 1.0);
    
    // Parâmetros da LUT 32x32x32 em formato 32x1024
    const float lutSize = 32.0;
    const float scale = (lutSize - 1.0) / lutSize;
    const float offset = 1.0 / (2.0 * lutSize);
    
    // Calcular slice do canal azul
    float blueSlice = color.b * (lutSize - 1.0);
    float blueSliceFloor = floor(blueSlice);
    float blueSliceCeil = min(blueSliceFloor + 1.0, lutSize - 1.0);
    float blueSliceFraction = blueSlice - blueSliceFloor;
    
    // Coordenadas X para as slices (cada slice tem 32 pixels de largura)
    float slice1_x = blueSliceFloor / lutSize;
    float slice2_x = blueSliceCeil / lutSize;
    
    // Coordenadas dentro da slice
    vec2 coords1, coords2;
    
    // Slice inferior
    coords1.x = slice1_x + (color.r * scale + offset) / lutSize;
    coords1.y = color.g * scale + offset;
    
    // Slice superior
    coords2.x = slice2_x + (color.r * scale + offset) / lutSize;
    coords2.y = color.g * scale + offset;
    
    // Sample das duas slices
    vec3 color1 = texture(lut, coords1).rgb;
    vec3 color2 = texture(lut, coords2).rgb;
    
    // Interpolação trilinear entre as slices
    return mix(color1, color2, blueSliceFraction);
}

// Versão alternativa mais robusta da LUT
vec3 applyLUT3D_v2(vec3 color, sampler2D lut) {
    color = clamp(color, 0.0, 1.0);
    
    const float lutSize = 32.0;
    const float invLutSize = 1.0 / lutSize;
    const float scale = (lutSize - 1.0) * invLutSize;
    const float offset = 0.5 * invLutSize;
    
    // Quantizar canais para índices da LUT
    vec3 scaledColor = color * (lutSize - 1.0);
    vec3 floorColor = floor(scaledColor);
    vec3 fracColor = scaledColor - floorColor;
    
    // Calcular coordenadas base
    float blueSlice = floorColor.b;
    float redCoord = (floorColor.r + offset) * invLutSize;
    float greenCoord = (floorColor.g + offset);
    
    // Primeira slice (B)
    vec2 coords1 = vec2(
        blueSlice * invLutSize + redCoord,
        greenCoord * invLutSize
    );
    
    // Segunda slice (B+1)
    vec2 coords2 = vec2(
        min(blueSlice + 1.0, lutSize - 1.0) * invLutSize + redCoord,
        greenCoord * invLutSize
    );
    
    // Sample e interpolar
    vec3 sample1 = texture(lut, coords1).rgb;
    vec3 sample2 = texture(lut, coords2).rgb;
    
    return mix(sample1, sample2, fracColor.b);
}

// Correção matemática híbrida (fallback)
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
    
    // Preservar luminância
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
    
    vec3 correctedColor;
    
    if (useLUT) {
        // Usar LUT (método preferido se disponível)
        correctedColor = applyLUT3D_v2(originalColor, lutTexture);
    } else {
        // Usar correção matemática (fallback)
        correctedColor = hybridCorrection(originalColor);
    }
    
    // Misturar baseado na intensidade
    vec3 finalColor = mix(originalColor, correctedColor, correctionStrength);
    
    FragColor = vec4(finalColor, 1.0);
}