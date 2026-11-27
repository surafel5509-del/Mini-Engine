cmake_minimum_required(VERSION 3.22)
project(miniengine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Engine core sources (platform agnostic; JNI only on Android).
file(GLOB_RECURSE MINI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(miniengine STATIC ${MINI_SOURCES})
target_include_directories(miniengine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(miniengine PRIVATE -Wall -Wextra)

# Vulkan is optional on desktop and available from the Android NDK.
# Without a loader/SDK the renderer compiles as a safe fallback stub.
if(ANDROID)
    target_sources(miniengine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/jni/JniBridge.cpp)
    find_library(LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(VULKAN_LIB vulkan)
    target_link_libraries(miniengine PRIVATE ${LOG_LIB} ${ANDROID_LIB} GLESv3)
    if(VULKAN_LIB)
        target_compile_definitions(miniengine PRIVATE MINI_VULKAN=1 VK_USE_PLATFORM_ANDROID_KHR=1)
        target_link_libraries(miniengine PRIVATE ${VULKAN_LIB})
    endif()
else()
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_compile_definitions(miniengine PRIVATE MINI_VULKAN=1)
        target_link_libraries(miniengine PRIVATE Vulkan::Vulkan)
    endif()
endif()

# Desktop smoke test (not built for Android).
if(NOT ANDROID)
    add_executable(mini_smoke tests/smoke_test.cpp)
    target_link_libraries(mini_smoke PRIVATE miniengine)
    find_package(Threads REQUIRED)
    target_link_libraries(mini_smoke PRIVATE Threads::Threads)
endif()
