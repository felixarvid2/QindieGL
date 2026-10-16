cmake_minimum_required(VERSION 3.16)
project(QindieGL C CXX RC)

# Linux/MinGW build of the Windows opengl32.dll wrapper (runs under Wine/Proton).
file(GLOB QGL_SRC code/*.cpp)
file(GLOB IMGUI_SRC extern/imgui/*.cpp)
# idtech3_mixup (per-game hooks) still uses MSVC inline asm / __FUNCSIG__; not ported yet, so default to the
# existing QINDIEGLSRC_NO_REMIX configuration which compiles those hooks out.
option(QINDIEGL_NO_REMIX "Build without the idtech3_mixup hooks / RTX Remix API" ON)
file(GLOB MIXUP_SRC idtech3_mixup/*.cpp)
set(DETOURS_SRC
    extern/detours/src/detours.cpp extern/detours/src/modules.cpp extern/detours/src/disasm.cpp
    extern/detours/src/image.cpp extern/detours/src/creatwth.cpp extern/detours/src/disolx86.cpp
    extern/detours/src/disolx64.cpp extern/detours/src/disolia64.cpp extern/detours/src/disolarm.cpp
    extern/detours/src/disolarm64.cpp)
set(EXT_SRC
    extern/dxut/dxerr_a.cpp extern/dxut/dxerr_w.cpp extern/fnvhash/hash_32a.c
    extern/imgui/backends/imgui_impl_dx9.cpp extern/imgui/backends/imgui_impl_win32.cpp)


# windres cannot parse the Windows-style include path in the .rc on Linux; generate a fixed copy.
file(READ msvc/QindieGL.rc RC_TEXT)
string(REPLACE "..\\\\code\\\\resource.h" "resource.h" RC_TEXT "${RC_TEXT}")
file(WRITE ${CMAKE_BINARY_DIR}/QindieGL.rc "${RC_TEXT}")

add_library(qgl_objs OBJECT ${QGL_SRC} ${IMGUI_SRC} ${EXT_SRC})
if(NOT QINDIEGL_NO_REMIX)
    target_sources(qgl_objs PRIVATE ${MIXUP_SRC} ${DETOURS_SRC})
endif()
# MinGW cannot resolve the plain names in opengl32.def (see linux/gen-def.py), so generate a def from the objects.
set(GEN_DEF ${CMAKE_BINARY_DIR}/opengl32.gen.def)
add_custom_command(OUTPUT ${GEN_DEF}
    COMMAND python3 -I ${CMAKE_SOURCE_DIR}/linux/gen-def.py ${CMAKE_SOURCE_DIR}/msvc/opengl32.def
            ${LLVM_MINGW_ROOT}/bin/llvm-nm ${GEN_DEF} $<TARGET_OBJECTS:qgl_objs>
    DEPENDS qgl_objs ${CMAKE_SOURCE_DIR}/linux/gen-def.py ${CMAKE_SOURCE_DIR}/msvc/opengl32.def
    COMMAND_EXPAND_LISTS VERBATIM)
add_library(opengl32 SHARED $<TARGET_OBJECTS:qgl_objs> ${CMAKE_BINARY_DIR}/QindieGL.rc ${GEN_DEF})
set_target_properties(opengl32 PROPERTIES PREFIX "" SUFFIX ".dll")

target_include_directories(qgl_objs PRIVATE
    linux/shim code code/shaders code/gl_headers msvc
    extern/dxut extern/remix extern/fnvhash extern/imgui extern extern/mINI idtech3_mixup extern/detours/include extern/detours/src)
target_compile_definitions(qgl_objs PRIVATE
    WIN32 _M_IX86=600 _M_IX86_FP=2 _WINDOWS _USRDLL QINDIEGLSRC_EXPORTS NDEBUG SHADERS_FROM_SOURCE=1)
if(QINDIEGL_NO_REMIX)
    target_compile_definitions(qgl_objs PRIVATE QINDIEGLSRC_NO_REMIX)
endif()
target_compile_options(qgl_objs PRIVATE -O2 -msse2 -fpermissive -Wno-everything -include ${CMAKE_SOURCE_DIR}/linux/compat.h)
set_source_files_properties(${CMAKE_BINARY_DIR}/QindieGL.rc PROPERTIES COMPILE_FLAGS "-DSHADERS_FROM_SOURCE=1 -I${CMAKE_SOURCE_DIR}/code -I${CMAKE_SOURCE_DIR}/code/shaders")
target_link_options(opengl32 PRIVATE -static -Wl,--kill-at)
target_link_libraries(opengl32 PRIVATE d3dx9 d3d9 gdi32 user32 kernel32 dwmapi advapi32 shell32 ole32 oleaut32 uuid winmm)
