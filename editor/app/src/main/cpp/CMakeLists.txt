cmake_minimum_required(VERSION 3.22)
project(miniengine_app)

# Reuse the shared C++ engine core at the repository root.
set(MINI_ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../../engine)
add_subdirectory(${MINI_ENGINE_DIR} miniengine_build)

# Compile JNI entry points directly into the shared library loaded by Kotlin.
# If they live only in a static archive, the linker may discard that object
# because nothing references JNI symbols while building the shared library.
add_library(miniengine_jni SHARED ${MINI_ENGINE_DIR}/jni/JniBridge.cpp)
set_target_properties(miniengine_jni PROPERTIES OUTPUT_NAME miniengine)
target_link_libraries(miniengine_jni PRIVATE miniengine)
