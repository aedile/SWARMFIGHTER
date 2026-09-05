set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
idf_component_register(
    SRCS "${CORE_DIR}/galaga.c" "${CORE_DIR}/galaga_video.c" "${CORE_DIR}/galaga_sound.c" "${CORE_DIR}/z80/Z80.c"
    INCLUDE_DIRS "${CORE_DIR}" "${CORE_DIR}/z80"
    LDFRAGMENTS "linker.lf"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LSB_FIRST)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion)
