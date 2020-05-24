add_executable(sandbox WIN32 main.cpp lambda_event_listener.cpp)

include(FetchContent)
FetchContent_Declare(
    ultralight
    URL "https://ultralight-sdk.sfo2.cdn.digitaloceanspaces.com/ultralight-sdk-latest-win-x64.7z"
)
FetchContent_MakeAvailable(ultralight)


target_link_libraries(sandbox vtil binding UltralightCore AppCore Ultralight WebCore)
target_include_directories(sandbox PRIVATE "${ultralight_SOURCE_DIR}/include")
target_link_directories(sandbox PRIVATE "${ultralight_SOURCE_DIR}/lib")
