add_executable(sandbox main.cpp)

target_link_libraries(sandbox PRIVATE VTIL ws2_32)
