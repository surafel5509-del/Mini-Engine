cmake_minimum_required(VERSION 3.22)
project(miniengine_app)

# Reuse the shared C++ engine core at the repository root.
set(MINI_ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../../engine)
add_subdirectory(${MINI_ENGINE_DIR} miniengine_build)

# JNI entry points are compiled into the static engine target on Android.
# Give the JNI shared target a distinct CMake target name; its output name
# remains libminiengine.so for System.loadLibrary("miniengine").
add_library(miniengine_jni SHARED placeholder.cpp)
set_target_properties(miniengine_jni PROPERTIES OUTPUT_NAME miniengine)
target_link_libraries(miniengine_jni PRIVATE miniengine)
