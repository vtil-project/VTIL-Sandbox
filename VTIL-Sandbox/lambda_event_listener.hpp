#pragma once
#include <functional>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>
#include <AppCore/JSHelpers.h>
#include <Ultralight/platform/Platform.h>

using namespace ultralight;

struct lambda_event_listener : WindowListener, LoadListener, ViewListener
{
    // All callbacks we invoke upon receiving notifications for.
    //
    std::function<void()> on_close;
    std::function<void( View* )> on_begin_loading;
    std::function<void( View* )> on_finish_loading;
    std::function<void( View* )> on_dom_ready;
    std::function<void( uint32_t, uint32_t )> on_resize;
    std::function<void( View*, const String & url )> on_change_url;
    
    // The path to assets.
    //
    std::wstring assets_path;

    // Constructor takes the path to assets.
    //
    lambda_event_listener( const std::wstring& assets_path ) : assets_path( assets_path ) {}

    // Implement each event we hook.
    //
    virtual void OnClose() override;
    virtual void OnBeginLoading( View* caller ) override;
    virtual void OnChangeURL( View* caller, const String& url ) override;
    virtual void OnFinishLoading( View* caller ) override;
    virtual void OnDOMReady( View* caller ) override;
    virtual void OnResize( uint32_t width, uint32_t height ) override;
    virtual void OnAddConsoleMessage( View* caller, MessageSource source,
                                      MessageLevel level, const String& message,
                                      uint32_t line_number, uint32_t column_number, const String& source_id ) override;
};