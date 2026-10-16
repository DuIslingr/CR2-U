# cmake -DIN=<file> -DOUT=<file.inc> -P embed.cmake: the file as a C++ raw string literal (the GL
# device compiles the shaders' shared GLSL from it)
file(READ ${IN} text)
file(WRITE ${OUT} "R\"CR2GLSL(${text})CR2GLSL\"\n")
