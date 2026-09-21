cmake_minimum_required(VERSION 3.20)

project(deepseek_harness_launcher LANGUAGES CXX RC)

set(WINTOOLS_ROOT "D:/WinTools1.12/mingwVersion" CACHE PATH
    "Path to the WinTools MinGW distribution")

add_executable(deepseek-harness
    launcher/main.cpp
    launcher/app.rc
)
target_compile_features(deepseek-harness PRIVATE cxx_std_17)
target_compile_definitions(deepseek-harness PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0601
)
target_link_libraries(deepseek-harness PRIVATE shell32)

set_target_properties(deepseek-harness PROPERTIES
    OUTPUT_NAME "deepseek-harness"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    WIN32_EXECUTABLE TRUE
)

if(MINGW)
    target_link_options(deepseek-harness PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

if(EXISTS "${WINTOOLS_ROOT}/include/wtlFileManagement.hpp"
   AND EXISTS "${WINTOOLS_ROOT}/src/lib/libwtlFileManagementStatic.a"
   AND EXISTS "${WINTOOLS_ROOT}/src/lib/libwtlStringCodeStatic.a")
    add_executable(dsh-wintools-helper launcher/wintools_helper.cpp)
    target_compile_features(dsh-wintools-helper PRIVATE cxx_std_17)
    target_compile_definitions(dsh-wintools-helper PRIVATE
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0601
    )
    target_include_directories(dsh-wintools-helper PRIVATE
        "${WINTOOLS_ROOT}/include"
    )
    target_link_libraries(dsh-wintools-helper PRIVATE
        "${WINTOOLS_ROOT}/src/lib/libwtlFileManagementStatic.a"
        "${WINTOOLS_ROOT}/src/lib/libwtlStringCodeStatic.a"
    )
    set_target_properties(dsh-wintools-helper PROPERTIES
        OUTPUT_NAME "dsh-wintools-helper"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )

    if(MINGW)
        target_link_options(dsh-wintools-helper PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()
