#include "lambda_event_listener.hpp"
#include <vtil/io>
#include <Windows.h>

// Implement each event we hook.
//
void lambda_event_listener::OnClose()
{
	if ( on_close )
		on_close();
}
void lambda_event_listener::OnBeginLoading( View* caller )
{
	SetJSContext( caller->js_context() );
	if ( on_begin_loading )
		on_begin_loading( caller );
}
void lambda_event_listener::OnChangeURL( View* caller, const String& url )
{
	SetJSContext( caller->js_context() );
	if ( on_change_url )
		on_change_url( caller, url );
}
void lambda_event_listener::OnFinishLoading( View* caller )
{
	SetJSContext( caller->js_context() );
	if ( on_finish_loading )
		on_finish_loading( caller );
}
void lambda_event_listener::OnDOMReady( View* caller )
{
	SetJSContext( caller->js_context() );
	if ( on_dom_ready )
		on_dom_ready( caller );
}
void lambda_event_listener::OnResize( uint32_t width, uint32_t height )
{
	if ( on_resize )
		on_resize( width, height );
}
void lambda_event_listener::OnAddConsoleMessage( View* caller, MessageSource source, MessageLevel level, const String& message, uint32_t line_number, uint32_t column_number, const String& source_id )
{
	using namespace vtil::logger;

	// Initialize console if first time adding a console message.
	//
	static bool console_init = false;
	if ( !console_init )
	{
		FILE* tmp;
		AllocConsole();
		SetConsoleTitleA( "VTIL Sandbox" );
		freopen_s( &tmp, "CONOUT$", "w", stdout );
		freopen_s( &tmp, "CONOUT$", "w", stderr );
	}

	// Shorten the source name where possible.
	//
	std::wstring source_name = source_id.empty() ? L"" : source_id.utf16().data();
	if ( source_name.starts_with( assets_path ) )
		source_name = source_name.substr( assets_path.size() );

	// Log the message colored based on their level.
	//
	if ( level == kMessageLevel_Warning )
		log<CON_YLW>( "[%ls:%d] %ls\n", source_name, line_number, message.utf16().data() );
	else if ( level == kMessageLevel_Error )
		log<CON_RED>( "[%ls:%d] %ls\n", source_name, line_number, message.utf16().data() );
	else
		log<CON_BRG>( "[%ls:%d] %ls\n", source_name, line_number, message.utf16().data() );
}
