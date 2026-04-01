# Garbage cmake just to get it working

cmake_minimum_required(VERSION 3.14.5)

project(VTIL-Sandbox)

# Set default C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)

option(VTIL_SANDBOX_BUILD_FRONTEND "Build Vue frontend as part of the CMake build" ON)

add_subdirectory(VTIL-Core build-vtil-core)
add_subdirectory(VTIL-Sandbox)

if(VTIL_SANDBOX_BUILD_FRONTEND)
    find_program(NPM_EXECUTABLE npm)
    if(NPM_EXECUTABLE)
        add_custom_target(
            sandbox-frontend
            COMMAND ${NPM_EXECUTABLE} install
            COMMAND ${NPM_EXECUTABLE} run build
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/VTIL-SandboxFrontend"
        )
    else()
        message(WARNING "VTIL_SANDBOX_BUILD_FRONTEND is ON but npm was not found. Skipping frontend build.")
    endif()
endif()