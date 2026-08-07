# Fetch dependencies
include(FetchContent)

# Several fetched deps default their own test-suite builds to this standard
# CTest switch, which some other dependency already flips ON as a side effect
# of calling enable_testing() - force it off so none of them build tests we
# don't run (e.g. tinyxml2's xmltest.exe).
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Silences every warning in a FetchContent'd dependency's targets, including
# ones defined in its own nested add_subdirectory() calls (e.g. bgfx's
# bx/bimg/shaderc) - so raising our own project's warning level doesn't also
# start failing the build on third-party code we don't control.
function(disable_third_party_warnings dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)

    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)

        if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            # Strip any warning-level flag the dependency set for itself
            # first (e.g. /W3, /W4) - otherwise it's still present earlier
            # on the command line and MSVC reports "overriding '/W3' with
            # '/w'" for every one of its translation units. Matched as a
            # substring, not a full-string match: some deps (e.g. SDL3, see
            # cmake/sdlcompilers.cmake) set it wrapped in a generator
            # expression like "$<$<COMPILE_LANGUAGE:C,CXX>:/W3>" rather than
            # as a bare token.
            get_target_property(existing_opts ${target} COMPILE_OPTIONS)

            if(existing_opts)
                list(FILTER existing_opts EXCLUDE REGEX "[-/]W(0|1|2|3|4|all|extra|pedantic)([^a-zA-Z0-9_]|$)")
                set_property(TARGET ${target} PROPERTY COMPILE_OPTIONS "${existing_opts}")
            endif()

            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:MSVC>:/w>
                $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)
        endif()
    endforeach()

    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

    foreach(subdir IN LISTS subdirs)
        disable_third_party_warnings("${subdir}")
    endforeach()
endfunction()

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 6f14f4792a0cde5d0cf2c910506724d61cb95834
    SYSTEM
)

FetchContent_MakeAvailable(glm)
disable_third_party_warnings(${glm_SOURCE_DIR})

# tinyxml
set(tinyxml2_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE) # skip xmltest.exe, not used by us

FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 11.0.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SYSTEM
)

FetchContent_MakeAvailable(tinyxml2)
disable_third_party_warnings(${tinyxml2_SOURCE_DIR})

# fastgltf
FetchContent_Declare(fastgltf URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.9.0.tar.gz)
FetchContent_MakeAvailable(fastgltf)
disable_third_party_warnings(${fastgltf_SOURCE_DIR})

# SDL3
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.16
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SYSTEM
)

FetchContent_MakeAvailable(SDL3)
disable_third_party_warnings(${sdl3_SOURCE_DIR})

# bgfx
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Tools: only the shader compiler (shaderc) is needed, to build our own .sc
# shaders below - texturec/geometryc/bin2c aren't used (textures are decoded
# via bimg at runtime, meshes are generated procedurally in C++).
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)

set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG v1.151.9375-560
    GIT_SUBMODULES_RECURSE TRUE
    SYSTEM
)

FetchContent_MakeAvailable(bgfx)
disable_third_party_warnings(${bgfx_SOURCE_DIR})

# Set source files
set(SOURCES
    "src/main.cpp"
    "src/core/engine.cpp"
    "src/serialization/serialization.cpp"
    "src/core/scene_manager.cpp"
    "src/serialization/xml_parser.cpp"
    "src/renderer/renderer.cpp"
    "src/core/window.cpp"
    "src/core/transform.cpp"
    
 "src/temp/camera_movement.cpp" "src/core/input_manager.cpp")

# Add executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()

# bgfx links bx/bimg PRIVATEly, so its own consumers (us) need them named
# explicitly to use bx::math and bimg::imageParse directly (see renderer.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm tinyxml2::tinyxml2 fastgltf::fastgltf SDL3::SDL3 bgfx bx bimg bimg_decode)

# Set include directories
target_include_directories(${PROJECT_NAME} PRIVATE "src" "src/serialization" "src/ecs" "src/renderer" "src/core")

# Compile the PBR shaders for every profile this platform needs; bgfx_compile_shaders()
# picks the right ones (dxbc/dxil/spirv/glsl/essl/metal) and Renderer picks the matching
# folder at runtime based on bgfx::getRendererType()
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/src)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/bin)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${SHADER_SRC_DIR}/vs_pbr.sc
    VARYING_DEF ${SHADER_SRC_DIR}/varying.def.sc
    OUTPUT_DIR ${SHADER_BIN_DIR}
    OUT_FILES_VAR PBR_VS_OUTPUTS
    INCLUDE_DIRS ${BGFX_DIR}/src
    DEFINES MAX_POINT_LIGHTS=4
)

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${SHADER_SRC_DIR}/fs_pbr.sc
    VARYING_DEF ${SHADER_SRC_DIR}/varying.def.sc
    OUTPUT_DIR ${SHADER_BIN_DIR}
    OUT_FILES_VAR PBR_FS_OUTPUTS
    INCLUDE_DIRS ${BGFX_DIR}/src
    DEFINES MAX_POINT_LIGHTS=4
)

add_custom_target(pbr_shaders DEPENDS ${PBR_VS_OUTPUTS} ${PBR_FS_OUTPUTS})
add_dependencies(${PROJECT_NAME} pbr_shaders)

# Copy data folder to output directory after every build, preserving structure
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/data
    COMMENT "Copying data folder to output directory"
)

# Copy SDL3's shared library next to the executable after every build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying SDL3 shared library to output directory"
)