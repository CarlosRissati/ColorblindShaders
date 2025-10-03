# ColorblindShaders

O filtro funciona apenas em versão do windows 10 para cima.
As luts foram pegas do reposítorio [adrewwillmott/colour-blind-luts](https://github.com/andrewwillmott/colour-blind-luts)

## Build

Para poder fazer uma build do projeto você deve primeiro ter `vcpkg` instalado ou `MSYS2`.
Você pode clonar o `vcpkg` direto do reposótiro da Microsoft [link](https://github.com/Microsoft/vcpkg)
Já o `MSYS2` você pode baixar através deste [link](https://www.msys2.org/)
As seguintes depências precisam ser instaladas:

*O repositório ja providência as bibliotecas do `glad` e do `stb`, porém fica ao seu critério instalar elas.

#### vcpkg:

```sh
vcpkg install glfw3:x64-mingw-dynamic
vcpkg install glad:x64-mingw-dynamic
vcpkg install glm:x64-mingw-dynamic
vcpkg install stb:x64-mingw-dynamic
vcpkg integrate install
```

#### MSYS2

```sh
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-pkg-config
pacman -S mingw-w64-x86_64-glfw
pacman -S mingw-w64-x86_64-glm
```

Lembre-se de adiconar o PATH das bibliotecas que você instalou às variáveis do sistema:
E.X. `vcpkg` -> "`C:\vcpkg\installed\x64-mingw-dynamic\bin`"

Certifique-se de que seu `MinGW` está no PATH também:
"`C:\msys64\mingw64\bin`"

### Configurações VSCODE

Altere o arquivo `.vscode/c_cpp_properties.json` com as informações citadas dentro dele.

Por fim rode os comandos do cmake para compilar a build:
```sh
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

O aplicativo irá ser encontrado na pasta `build/DaltonismoFilter.exe`, caso deseja alterar a LUT de aplicação, altere dentro da pasta `build/luts` colocando o novo .png da LUT e alterando seu nome para `deuteranopia_correction`. Caso deseje mudar este nome de arquivo, pode ser alterado também na `src/main.cpp` próximo a linha "532" -> `if (!lutLoader->loadLUT("luts/deuteranopia_correction.png")) ...`

