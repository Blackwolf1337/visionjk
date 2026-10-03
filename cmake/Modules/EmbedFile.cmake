# Turns a file into a C header with a byte array.
# cmake -DINPUT=file -DOUTPUT=header.h -DNAME=symbol -P EmbedFile.cmake
file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" hexLength)
math(EXPR length "${hexLength} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "// generated from ${INPUT}, do not edit\nstatic const unsigned char ${NAME}[] = {\n${bytes}0x00\n};\nstatic const unsigned int ${NAME}_len = ${length};\n")
