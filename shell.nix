{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # Compiler and build tools
    gcc14
    cmake
    gnumake
    pkg-config
    git

    # Libraries
    SDL2
    SDL2.dev
    libopenmpt
    mpg123.dev
    libogg.dev
    libvorbis.dev
    curl.dev
    libGL
    glew
    glew.dev
  ];

  shellHook = ''
    echo "mod_player development environment"
    echo "  gcc:        $(gcc --version | head -1)"
    echo "  cmake:      $(cmake --version | head -1)"
    echo "  SDL2:       $(pkg-config --modversion sdl2)"
    echo "  libopenmpt: $(pkg-config --modversion libopenmpt)"
    echo "  libcurl:    $(pkg-config --modversion libcurl)"
    echo ""
    echo "Build with: mkdir -p build && cd build && cmake .. && make"
  '';
}
