# Garbage cmake just to get it working

cmake_minimum_required(VERSION 3.14.5)

project(VTIL-Sandbox)

# Set default C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)


add_subdirectory(../VTIL-Core build-vtil-core)
add_subdirectory(VTIL-Binding)
add_subdirectory(VTIL-Sandbox)

add_custom_target(
    sandbox-frontend
    COMMAND npm run build
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/vtil-sandboxfrontend"
)

add_dependencies(sandbox sandbox-frontend)