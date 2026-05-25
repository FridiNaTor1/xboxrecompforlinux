# D3D8 → D3D11 Compatibility Layer
# Implements Xbox D3D8 interface backed by Direct3D 11

if(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "d3d11")
    add_library(xbox_d3d8 STATIC
        d3d8_device.c
        d3d8_resources.c
        d3d8_shaders.c
        d3d8_states.c
        d3d8_combiners.c
        d3d8_vsh.c
    )
else()
    add_library(xbox_d3d8 STATIC
        d3d8_null.c
    )
endif()

target_include_directories(xbox_d3d8 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${XBOXRECOMP_PLATFORM_COMPAT_INCLUDE}
)

if(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "d3d11")
    target_link_libraries(xbox_d3d8 PUBLIC d3d11 dxgi dxguid d3dcompiler)
else()
    target_compile_definitions(xbox_d3d8 PUBLIC XBOXRECOMP_NULL_GRAPHICS=1)
endif()

if(MSVC)
    target_compile_options(xbox_d3d8 PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_d3d8 PRIVATE -Wall -Wextra -O2)
endif()
