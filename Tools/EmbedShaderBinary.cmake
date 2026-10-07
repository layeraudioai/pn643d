if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "INPUT and OUTPUT are required")
endif()
file(READ "${INPUT}" shader_hex HEX)
string(LENGTH "${shader_hex}" shader_hex_length)
if(shader_hex_length EQUAL 0 OR shader_hex_length GREATER 131072)
  message(FATAL_ERROR "Shader binary is empty or exceeds 64 KiB")
endif()
string(REGEX REPLACE "(..)" "0x\\1," shader_bytes "${shader_hex}")
file(WRITE "${OUTPUT}"
  "/* Generated from the profile-only PICA shader binary. */\n"
  "#include <stddef.h>\n"
  "const unsigned char nintenstation_affine_shader[] __attribute__((aligned(4))) = {${shader_bytes}};\n"
  "const unsigned int nintenstation_affine_shader_size = sizeof(nintenstation_affine_shader);\n")
