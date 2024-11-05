set(include_dirs 
    "include"
)
set(srcs
    "src/aes_modes.c"
    "src/aes.c"
    "src/block_cipher.c"
    "src/gcm.c"
    "src/gf128.c"
    "src/hex.c"
    "src/rand.c"
    "src/sm4_common.c"
    "src/sm4_enc.c"
    "src/sm4_modes.c"
    "src/sm4_setkey.c"
    "src/version.c"
)

idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${include_dirs})
