#include <memory>
#include <vtil/amd64>
#include <vtil/vtil>
#include <vtil/arch>
#include <Windows.h>
#include <fstream>
#include <AppCore/App.h>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>
#include <Ultralight/platform/Platform.h>
#include <AppCore/JSHelpers.h>
#include "bindings/basic_block.hpp"
#include "lambda_event_listener.hpp"
#include "resource.h"

// Default state of the views.
//
const std::wstring default_path = L"index.html";
const float default_width = GetSystemMetrics( SM_CXSCREEN ) * 0.6f;
const float default_heigth = GetSystemMetrics( SM_CYSCREEN ) * 0.6f;

// Ultralight application state.
//
using namespace ultralight;
RefPtr<App> app;
RefPtr<Window> window;
RefPtr<Overlay> main_overlay;



// Current VTIL routine we're inspecting.
//
std::wstring file_name;
vtil::routine* routine;

// Path to assets and the common event listener.
//
const std::wstring assets_path = L"http://localhost:8080/";
lambda_event_listener event_listener( assets_path );

// Pops a file dialogue with the given filter.
//
std::wstring pop_file_dialogue( const wchar_t* filter )
{
    // Allocate buffer for the API.
    //
    wchar_t buffer[ MAX_PATH ] = { 0 };

    // Set the dialogue parameters.
    //
    OPENFILENAMEW open_file_name;
    memset( &open_file_name, 0, sizeof( open_file_name ) );
    open_file_name.lStructSize = sizeof( OPENFILENAMEW );
    open_file_name.hwndOwner = NULL;
    open_file_name.lpstrFile = buffer;
    open_file_name.nMaxFile = MAX_PATH;
    open_file_name.lpstrFilter = filter;
    open_file_name.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // If API succeeds, set as the target file.
    //
    if ( GetOpenFileNameW( &open_file_name ) )
        return buffer;
    else
        return {};
}

// Attempts to load a VTIL routine from the given path.
//
bool load_routine( const std::wstring& path )
{
    vtil::basic_block* blk = vtil::basic_block::begin( 0x100 );
    auto [t0, t1] = blk->tmp( 64, 64 );

    blk->pop( t0 )
        ->mov( t1, t0 )
        ->jmp( t0 );


    blk->fork( 0x200 )
        ->vexit( 0x1337 );

    blk->fork( 0x300 )
        ->vexit( 0x1337  );

    vtil::debug::dump(blk->owner);


    routine = blk->owner;

    /*try
    {
        // Create the input file stream and try deserializing.
        //
        std::ifstream fs( "test.vtil", std::ios::binary );
        vtil::routine* output = nullptr;
        vtil::deserialize( fs, output );

        // If it did not throw an exception, swap with the current routine.
        //
        routine = std::unique_ptr<vtil::routine>( output );
        file_name = path.substr( path.find_last_of( '\\' ) + 1 );
        return true;
    }
    // If there was an exception, report failure.
    //
    catch ( const std::exception & ex )
    {
        return false;
    }*/


    return true;
}

// Exports the menu API.
//
void export_menu_api( JSObject& vtil_object )
{
    // Callback to execute a script in the main view.
    //
    vtil_object[ "run" ] = JSCallbackWithRetval( [ ] ( const JSObject& thisObject, const JSArgs& args ) -> JSValue
    {
        // Pop a file dialogue and try to read a .js file.
        //
        std::ifstream ss( pop_file_dialogue( L"VTIL Scripts\0*.js\x0" ) );
        if ( !ss )
            return false;
        std::string str( ( std::istreambuf_iterator<char>( ss ) ),
                           std::istreambuf_iterator<char>() );

        SetJSContext( main_overlay->view()->js_context() );
        JSEval( str.data() );
        return true;
    } );

    // Callback to load a new file.
    //
    vtil_object[ "load" ] = JSCallbackWithRetval( [ ] ( const JSObject& thisObject, const JSArgs& args ) -> JSValue
    {
        // Pop a file dialogue and try to read a new VTIL routine, if it fails report failure.
        //
        std::wstring target_file = pop_file_dialogue( L"VTIL Intermediate Files\0*.vtil\x0" );
        if ( !load_routine( target_file ) )
            return false;

        // Reload the main view and report success.
        //
        main_overlay->view()->Reload();
        return true;
    } );

    // Callback to reload all windows.
    //
    vtil_object[ "reload" ] = JSCallback( [ ] ( const JSObject& thisObject, const JSArgs& args )
    {
        main_overlay->view()->Reload();
    } );

    // Callback to get and set current main view.
    //
    vtil_object[ "get_view" ] = JSCallbackWithRetval( [ ] ( const JSObject& thisObject, const JSArgs& args )
    {
        std::wstring path = vtil::js::from_js<std::wstring>( main_overlay->view()->url() );
        if ( path.starts_with( assets_path ) )
            path = path.substr( assets_path.size() );
        return vtil::js::as_js( path );
    } );
    vtil_object[ "set_view" ] = JSCallback( [ ] ( const JSObject& thisObject, const JSArgs& args )
    {
        fassert( args.size() >= 1 && args.data()[ 0 ].IsString() );
        main_overlay->view()->LoadURL( String{ assets_path.data(), assets_path.length() } + args.data()[ 0 ].ToString() );
    } );
}

// Exports the view API.
//
void export_view_api( JSObject& vtil_object )
{
    // Export instruction list.
    //
    JSObject list_out;
    for ( auto& instruction : vtil::instruction_list )
        list_out[ vtil::js::as_js( instruction.name ) ] = vtil::js::as_js( &instruction );
    vtil_object[ "ins" ] = JSValue( list_out );

    // Export all blocks.
    //
    JSObject block_map;
    for ( auto& pair : routine->explored_blocks )
        block_map[ vtil::js::as_js( pair.first ) ] = vtil::js::as_js( pair.second );
    vtil_object[ "blocks" ] = JSValue( block_map );

    // Export entry point.
    //
    vtil_object[ "entry_point" ] = vtil::js::as_js( routine->entry_point->entry_vip );
}

// Entry point of the application.
//
int WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd )
{
    // Try fetching the file name from the command line. If no
    // input file specified, pop a file dialogue.
    //
    std::wstring target_file = std::wstring( lpCmdLine, lpCmdLine + strlen( lpCmdLine ) );
    if ( target_file.empty() )
        target_file = pop_file_dialogue( L"VTIL Intermediate Files\0*.vtil\x0" );

    // Try loading the VTIL routine from the path, if we could not
    // throw an error and quit the application.
    //
    if ( !load_routine( target_file ) )
    {
        MessageBoxA( 0, "Could not read the file.", "VTIL Sandbox - Error", MB_ICONERROR );
        return -2;
    }

    // Set the listener for window resize.
    //
    event_listener.on_resize =  [ ]( uint32_t width, uint32_t height )
    {
        main_overlay->Resize( width, height );
    };

    // Set the listener for URL being changed to export the API.
    //
    event_listener.on_change_url = [ ] ( View* view, auto& )
    {
        JSObject vtil_object = {};

        vtil_object[ "file_name" ] = vtil::js::as_js( file_name );

        if ( view == main_overlay->view().ptr() )
            export_menu_api( vtil_object );
        else if ( view == main_overlay->view().ptr() )
            export_view_api( vtil_object );

        JSGlobalObject()[ "vtil" ] = JSValue( vtil_object );
    };

    // Create the ultralight app and the window.
    //
    app = App::Create();
    window = Window::Create( app->main_monitor(), default_width, default_heigth, false, kWindowFlags_Titled | kWindowFlags_Resizable );
    app->set_window( *window.get() );
    window->SetTitle( "VTIL Sandbox" );

    // Load icon.
    //
    EnumWindows( [ ] ( HWND window_handle, auto ) -> BOOL
    {
        DWORD pid = 0;
        GetWindowThreadProcessId( window_handle, &pid );
        if ( pid == GetCurrentProcessId() )
        {
            HICON icon = LoadIconA( GetModuleHandleA(0), MAKEINTRESOURCEA( IDI_ICON1 ) );
            SendMessageA( window_handle, WM_SETICON, ICON_SMALL, LPARAM( icon ) );
        }
        return TRUE;
    }, 0 );

    // Create the panes and resize accordingly.
    //
    main_overlay = Overlay::Create( *window.get(), 1, 1, 0, 0 );
    event_listener.OnResize( window->width(), window->height() );

    // Set the listeners.
    //
    window->set_listener( &event_listener );
    main_overlay->view()->set_load_listener( &event_listener );
    main_overlay->view()->set_view_listener( &event_listener );

    // Navigate to the default pages.
    //
    std::wstring menu_path = assets_path + default_path;
    main_overlay->view()->LoadURL( String{ menu_path.data(), menu_path.length() } );

    // Run the app.
    //
    app->Run();
    return 0;
}