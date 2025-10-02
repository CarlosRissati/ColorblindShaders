#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main()
{
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    // CORREÇÃO: Inverter coordenada Y para corrigir tela invertida
    TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
}