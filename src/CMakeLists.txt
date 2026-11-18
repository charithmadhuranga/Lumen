# Lumen application component (SPEC §22 source tree).
# Components under src/ (core, hardware, lighting, automation, networking,
# web, services) register themselves as phases land; each component
# registers only what it uses.

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "." "../include"
)

# AGENTS.md §5: C++17 (applies to C++ sources only; C files are unaffected).
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
