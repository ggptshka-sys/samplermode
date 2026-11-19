cmake_minimum_required(VERSION 3.25)
project(SamplerMode VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SamplerMode currently supports Windows x64 only")
endif()

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_BUNDLE_FOR_WINDOWS ON CACHE BOOL "" FORCE)
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/vst3sdk/CMakeLists.txt")
    set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.deps/vst3sdk")
    add_subdirectory(.deps/vst3sdk vst3sdk)
else()
    include(FetchContent)
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.7.14_build_55
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(vst3sdk)
    set(VST3_SDK_PATH "${vst3sdk_SOURCE_DIR}")
endif()

set(sampler_core
    src/sampler_engine.cpp
    src/sampler_engine.h
    src/library_win.cpp
)

smtg_add_vst3plugin(sampler
    ${sampler_core}
    src/plugin.cpp
    src/plugin.h
    src/editor_win32.cpp
    src/editor_win32.h
    "${VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp"
)

target_compile_features(sampler PRIVATE cxx_std_17)
target_compile_definitions(sampler PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(sampler PRIVATE /utf-8)
target_link_libraries(sampler PRIVATE sdk comctl32 oleacc ole32 shell32 uuid)
set_property(TARGET sampler PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_custom_command(TARGET sampler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sampler>" "${CMAKE_BINARY_DIR}/package/sampler.dll"
)

add_executable(sampler-diagnostic
    ${sampler_core}
    src/diagnostic.cpp
)
target_compile_features(sampler-diagnostic PRIVATE cxx_std_17)
target_compile_definitions(sampler-diagnostic PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(sampler-diagnostic PRIVATE /utf-8)
set_property(TARGET sampler-diagnostic PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(sampler-diagnostic PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/$<0:>")
