#define NOMINMAX
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <vtil/io>
#include <vtil/vtil>

#pragma comment( lib, "ws2_32.lib" )

std::wstring file_name;
std::unique_ptr<vtil::routine> routine;
std::wstring last_error;
std::mutex state_mutex;

std::once_flag console_init_once;
std::atomic<int> active_clients{ 0 };
std::atomic<bool> server_running{ true };
constexpr int max_active_clients = 64;
constexpr size_t max_header_bytes = 64 * 1024;
constexpr size_t max_upload_bytes = 32 * 1024 * 1024;
constexpr int socket_timeout_ms = 15000;

enum class backend_log_level
{
	info,
	warning,
	error,
	trace
};

void debug_log( const std::wstring& message, backend_log_level level = backend_log_level::info );

struct socket_guard
{
	SOCKET sock = INVALID_SOCKET;
	explicit socket_guard( SOCKET s ) : sock( s ) {}
	~socket_guard()
	{
		if ( sock != INVALID_SOCKET )
			closesocket( sock );
	}
};

struct active_client_guard
{
	std::atomic<int>& ctr;
	explicit active_client_guard( std::atomic<int>& c ) : ctr( c ) { ctr.fetch_add( 1 ); }
	~active_client_guard() { ctr.fetch_sub( 1 ); }
};

BOOL WINAPI console_ctrl_handler( DWORD ctrl_type )
{
	switch ( ctrl_type )
	{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			server_running.store( false );
			debug_log( L"Shutdown signal received, stopping server...", backend_log_level::warning );
			return TRUE;
		default:
			return FALSE;
	}
}

void initialize_console()
{
	std::call_once( console_init_once, [] {
		if ( !AttachConsole( ATTACH_PARENT_PROCESS ) )
			AllocConsole();

		FILE* tmp = nullptr;
		freopen_s( &tmp, "CONOUT$", "w", stdout );
		freopen_s( &tmp, "CONOUT$", "w", stderr );
		SetConsoleOutputCP( CP_UTF8 );
		SetConsoleTitleW( L"VTIL Sandbox Backend" );
	} );
}

void debug_log( const std::wstring& message, backend_log_level level )
{
	using namespace vtil::logger;

	initialize_console();
	const wchar_t* level_tag = L"INFO";
	console_color color = CON_BRG;
	if ( level == backend_log_level::warning )
	{
		level_tag = L"WARN";
		color = CON_YLW;
	}
	else if ( level == backend_log_level::error )
	{
		level_tag = L"ERR";
		color = CON_RED;
	}
	else if ( level == backend_log_level::trace )
	{
		level_tag = L"TRACE";
		color = CON_CYN;
	}

	std::wstring line = L"[VTIL-Sandbox][" + std::wstring( level_tag ) + L"] " + message + L"\r\n";
	OutputDebugStringW( line.c_str() );
	log<CON_DEF>( "" );
	if ( color == CON_YLW )
		log<CON_YLW>( "[%ls] %ls\n", level_tag, message.c_str() );
	else if ( color == CON_RED )
		log<CON_RED>( "[%ls] %ls\n", level_tag, message.c_str() );
	else if ( color == CON_CYN )
		log<CON_CYN>( "[%ls] %ls\n", level_tag, message.c_str() );
	else
		log<CON_BRG>( "[%ls] %ls\n", level_tag, message.c_str() );
}

std::string wide_to_utf8( const std::wstring& text )
{
	if ( text.empty() )
		return {};

	const int required = WideCharToMultiByte( CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr );
	if ( required <= 1 )
		return {};

	std::string output( required - 1, '\0' );
	WideCharToMultiByte( CP_UTF8, 0, text.c_str(), -1, output.data(), required, nullptr, nullptr );
	return output;
}

std::wstring utf8_to_wide( const std::string& text )
{
	if ( text.empty() )
		return {};

	const int required = MultiByteToWideChar( CP_UTF8, 0, text.c_str(), -1, nullptr, 0 );
	if ( required <= 1 )
		return {};

	std::wstring output( required - 1, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, text.c_str(), -1, output.data(), required );
	return output;
}

std::string json_escape( const std::string& input )
{
	std::ostringstream out;
	for ( char c : input )
	{
		switch ( c )
		{
			case '"':
				out << "\\\"";
				break;
			case '\\':
				out << "\\\\";
				break;
			case '\b':
				out << "\\b";
				break;
			case '\f':
				out << "\\f";
				break;
			case '\n':
				out << "\\n";
				break;
			case '\r':
				out << "\\r";
				break;
			case '\t':
				out << "\\t";
				break;
			default:
				if ( static_cast<unsigned char>( c ) < 0x20 )
				{
					out << "\\u" << std::uppercase << std::hex << std::setw( 4 ) << std::setfill( '0' ) << static_cast<unsigned int>( static_cast<unsigned char>( c ) )
						<< std::nouppercase << std::dec << std::setfill( ' ' );
				}
				else
					out << c;
				break;
		}
	}
	return out.str();
}

std::string make_json_ok() { return "{\"ok\":true}"; }

std::string make_json_error( const std::string& msg ) { return "{\"ok\":false,\"error\":\"" + json_escape( msg ) + "\"}"; }

std::string hex_u64( uint64_t value )
{
	std::ostringstream out;
	out << "0x" << std::hex << value;
	return out.str();
}

std::string to_upper_ascii( std::string s )
{
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
	return s;
}

std::string operand_type_to_string( vtil::operand_type type )
{
	switch ( type )
	{
		case vtil::operand_type::read_imm:
			return "read_imm";
		case vtil::operand_type::read_reg:
			return "read_reg";
		case vtil::operand_type::read_any:
			return "read_any";
		case vtil::operand_type::write:
			return "write";
		case vtil::operand_type::readwrite:
			return "readwrite";
		default:
			return "invalid";
	}
}

void append_instruction_desc_json( std::ostringstream& out, const vtil::instruction_desc* desc )
{
	if ( !desc )
	{
		out << "null";
		return;
	}

	out << "{";
	out << "\"name\":\"" << json_escape( desc->name ) << "\",";
	out << "\"is_volatile\":" << ( desc->is_volatile ? "true" : "false" ) << ",";
	out << "\"memory_write\":" << ( desc->memory_write ? "true" : "false" ) << ",";
	out << "\"access_size_index\":" << desc->vaccess_size_index << ",";
	out << "\"memory_operand_index\":" << desc->memory_operand_index << ",";

	out << "\"operand_types\":[";
	for ( size_t i = 0; i < desc->operand_types.size(); ++i )
	{
		if ( i ) out << ",";
		out << "\"" << operand_type_to_string( desc->operand_types[ i ] ) << "\"";
	}
	out << "],";

	out << "\"branch_operands_rip\":[";
	for ( size_t i = 0; i < desc->branch_operands_rip.size(); ++i )
	{
		if ( i ) out << ",";
		out << desc->branch_operands_rip[ i ];
	}
	out << "],";

	out << "\"branch_operands_vip\":[";
	for ( size_t i = 0; i < desc->branch_operands_vip.size(); ++i )
	{
		if ( i ) out << ",";
		out << desc->branch_operands_vip[ i ];
	}
	out << "]";
	out << "}";
}

void append_operand_json( std::ostringstream& out, const vtil::operand& op, vtil::operand_type access_type )
{
	out << "{";
	out << "\"type\":\"" << operand_type_to_string( access_type ) << "\",";
	out << "\"text\":\"" << json_escape( op.to_string() ) << "\",";
	out << "\"bit_count\":" << op.bit_count() << ",";
	out << "\"kind\":\"" << ( op.is_register() ? "register" : "immediate" ) << "\"";

	if ( op.is_register() )
	{
		auto reg = op.reg();
		out << ",\"register\":{";
		out << "\"name\":\"" << json_escape( reg.to_string() ) << "\",";
		out << "\"combined_id\":" << reg.combined_id << ",";
		out << "\"bit_offset\":" << reg.bit_offset << ",";
		out << "\"bit_count\":" << reg.bit_count;
		out << "}";
	}
	else
	{
		auto imm = op.imm();
		out << ",\"immediate\":{";
		out << "\"u64\":" << static_cast<unsigned long long>( imm.uval ) << ",";
		out << "\"i64\":" << static_cast<long long>( imm.ival ) << ",";
		out << "\"bit_count\":" << imm.bit_count;
		out << "}";
	}

	out << "}";
}

std::string format_display_instruction( const vtil::instruction& insn )
{
	const std::string mnemonic = to_upper_ascii( insn.base ? insn.base->name : "" );
	std::ostringstream out;
	out << mnemonic;
	if ( !insn.operands.empty() )
	{
		out << " ";
		for ( size_t i = 0; i < insn.operands.size(); ++i )
		{
			if ( i )
				out << ", ";
			out << insn.operands[ i ].to_string();
		}
	}
	return out.str();
}

void set_last_error( const std::wstring& message )
{
	{
		std::lock_guard<std::mutex> guard( state_mutex );
		last_error = message;
	}
	debug_log( message, backend_log_level::error );
}

std::string get_last_error_utf8()
{
	std::lock_guard<std::mutex> guard( state_mutex );
	return wide_to_utf8( last_error );
}

bool load_routine_from_bytes( const std::vector<uint8_t>& bytes, const std::wstring& source_name )
{
	if ( bytes.empty() )
	{
		set_last_error( L"Input VTIL payload is empty." );
		return false;
	}

	try
	{
		std::stringstream buffer( std::ios::in | std::ios::out | std::ios::binary );
		buffer.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
		buffer.seekg( 0 );

		vtil::routine* output_raw = nullptr;
		vtil::deserialize( buffer, output_raw );
		std::unique_ptr<vtil::routine> parsed( output_raw );
		if ( !parsed )
		{
			set_last_error( L"Deserializer returned null routine." );
			return false;
		}

		size_t block_count = 0;
		{
			std::lock_guard<std::mutex> guard( state_mutex );
			routine = std::move( parsed );
			file_name = source_name.empty() ? L"uploaded.vtil" : source_name;
			last_error.clear();
			block_count = routine->explored_blocks.size();
		}

		std::wstringstream ok;
		ok << L"Loaded VTIL upload successfully. blocks=" << block_count;
		debug_log( ok.str(), backend_log_level::info );
		return true;
	}
	catch ( const std::exception& ex )
	{
		set_last_error( utf8_to_wide( std::string( "load_routine_from_bytes exception: " ) + ex.what() ) );
		return false;
	}
	catch ( ... )
	{
		set_last_error( L"Unknown exception while loading uploaded VTIL payload." );
		return false;
	}
}

std::string make_state_json()
{
	std::lock_guard<std::mutex> guard( state_mutex );

	std::ostringstream out;
	out << "{";
	out << "\"ok\":" << ( routine ? "true" : "false" ) << ",";
	out << "\"file_name\":\"" << json_escape( wide_to_utf8( file_name ) ) << "\",";
	out << "\"last_error\":\"" << json_escape( wide_to_utf8( last_error ) ) << "\",";

	if ( routine && routine->entry_point )
		out << "\"entry_point\":\"" << hex_u64( routine->entry_point->entry_vip ) << "\",";
	else
		out << "\"entry_point\":null,";

	out << "\"blocks\":[";
	bool first_block = true;
	if ( routine )
	{
		for ( const auto& [ vip, blk ] : routine->explored_blocks )
		{
			if ( !first_block )
				out << ",";
			first_block = false;

			out << "{";
			out << "\"address\":\"" << hex_u64( vip ) << "\",";
			out << "\"instruction_count\":" << blk->size() << ",";

			out << "\"predecessors\":[";
			bool first_prev = true;
			for ( auto* prev_blk : blk->prev )
			{
				if ( !first_prev )
					out << ",";
				first_prev = false;
				out << "\"" << hex_u64( prev_blk->entry_vip ) << "\"";
			}
			out << "],";

			out << "\"successors\":[";
			bool first_next = true;
			for ( auto* next_blk : blk->next )
			{
				if ( !first_next )
					out << ",";
				first_next = false;
				out << "\"" << hex_u64( next_blk->entry_vip ) << "\"";
			}
			out << "],";

			out << "\"instructions\":[";
			bool first_insn = true;
			size_t index = 0;
			for ( auto it = blk->begin(); it != blk->end(); ++it, ++index )
			{
				const auto& insn = *it;
				if ( !first_insn )
					out << ",";
				first_insn = false;

				out << "{";
				out << "\"index\":" << index << ",";
				out << "\"vip\":\"" << hex_u64( insn.vip ) << "\",";
				out << "\"mnemonic\":\"" << json_escape( insn.base ? insn.base->name : "" ) << "\",";
				out << "\"text\":\"" << json_escape( insn.to_string() ) << "\",";
				out << "\"display_mnemonic\":\"" << json_escape( to_upper_ascii( insn.base ? insn.base->name : "" ) ) << "\",";
				out << "\"display_text\":\"" << json_escape( format_display_instruction( insn ) ) << "\",";
				out << "\"desc\":";
				append_instruction_desc_json( out, insn.base );
				out << ",";
				out << "\"operand_count\":" << insn.operands.size() << ",";
				out << "\"display_operands\":[";
				for ( size_t op_i = 0; op_i < insn.operands.size(); ++op_i )
				{
					if ( op_i )
						out << ",";
					out << "\"" << json_escape( insn.operands[ op_i ].to_string() ) << "\"";
				}
				out << "],";

				out << "\"operands\":[";
				for ( size_t op_i = 0; op_i < insn.operands.size(); ++op_i )
				{
					if ( op_i ) out << ",";
					vtil::operand_type access_type = vtil::operand_type::invalid;
					if ( insn.base && op_i < insn.base->operand_types.size() )
						access_type = insn.base->operand_types[ op_i ];
					append_operand_json( out, insn.operands[ op_i ], access_type );
				}
				out << "],";

				out << "\"is_branching\":" << ( insn.base && insn.base->is_branching() ? "true" : "false" ) << ",";
				out << "\"is_volatile\":" << ( insn.is_volatile() ? "true" : "false" ) << ",";
				out << "\"sp_offset\":" << insn.sp_offset << ",";
				out << "\"sp_index\":" << insn.sp_index;
				out << "}";
			}
			out << "]";
			out << "}";
		}
	}
	out << "],";

	out << "\"cfg_edges\":[";
	bool first_edge = true;
	if ( routine )
	{
		for ( const auto& [ vip, blk ] : routine->explored_blocks )
		{
			for ( auto* next_blk : blk->next )
			{
				if ( !first_edge )
					out << ",";
				first_edge = false;
				out << "{\"from\":\"" << hex_u64( vip ) << "\",\"to\":\"" << hex_u64( next_blk->entry_vip ) << "\"}";
			}
		}
	}
	out << "]";
	out << "}";
	return out.str();
}

std::string url_decode( const std::string& in )
{
	std::string out;
	out.reserve( in.size() );
	for ( size_t i = 0; i < in.size(); ++i )
	{
		if ( in[ i ] == '%' && i + 2 < in.size() )
		{
			int value = 0;
			std::istringstream is( in.substr( i + 1, 2 ) );
			is >> std::hex >> value;
			out.push_back( static_cast<char>( value ) );
			i += 2;
		}
		else if ( in[ i ] == '+' )
		{
			out.push_back( ' ' );
		}
		else
		{
			out.push_back( in[ i ] );
		}
	}
	return out;
}

bool recv_exact( SOCKET sock, std::string& data, size_t bytes_needed )
{
	while ( data.size() < bytes_needed )
	{
		char buffer[ 8192 ];
		int r = recv( sock, buffer, sizeof( buffer ), 0 );
		if ( r <= 0 )
			return false;
		data.append( buffer, r );
	}
	return true;
}

bool parse_content_length( const std::unordered_map<std::string, std::string>& headers, size_t& content_length, std::string& error )
{
	content_length = 0;
	auto it = headers.find( "Content-Length" );
	if ( it == headers.end() )
		return true;

	try
	{
		const unsigned long long parsed_len = std::stoull( it->second );
		if ( parsed_len > static_cast<unsigned long long>( std::numeric_limits<size_t>::max() ) )
		{
			error = "invalid content length";
			return false;
		}
		content_length = static_cast<size_t>( parsed_len );
		return true;
	}
	catch ( ... )
	{
		error = "invalid content length";
		return false;
	}
}

void send_response( SOCKET sock, int status, const std::string& body, const std::string& content_type = "application/json" )
{
	const char* reason = "OK";
	if ( status == 400 )
		reason = "Bad Request";
	else if ( status == 404 )
		reason = "Not Found";
	else if ( status == 500 )
		reason = "Internal Server Error";
	else if ( status == 503 )
		reason = "Service Unavailable";
	else if ( status == 204 )
		reason = "No Content";

	std::ostringstream headers;
	headers << "HTTP/1.1 " << status << " " << reason << "\r\n";
	headers << "Content-Type: " << content_type << "; charset=utf-8\r\n";
	headers << "Access-Control-Allow-Origin: *\r\n";
	headers << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
	headers << "Access-Control-Allow-Headers: Content-Type\r\n";
	headers << "Connection: close\r\n";
	headers << "Content-Length: " << body.size() << "\r\n\r\n";

	const std::string header_str = headers.str();
	send( sock, header_str.data(), static_cast<int>( header_str.size() ), 0 );
	if ( !body.empty() )
		send( sock, body.data(), static_cast<int>( body.size() ), 0 );
}

void handle_client( SOCKET client )
{
	try
	{
		socket_guard client_socket{ client };

		setsockopt( client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>( &socket_timeout_ms ), sizeof( socket_timeout_ms ) );
		setsockopt( client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>( &socket_timeout_ms ), sizeof( socket_timeout_ms ) );

		std::string request;
		if ( !recv_exact( client, request, 4 ) )
		{
			return;
		}

		while ( request.find( "\r\n\r\n" ) == std::string::npos )
		{
			if ( request.size() > max_header_bytes )
			{
				send_response( client, 400, make_json_error( "header too large" ) );
				return;
			}
			char buffer[ 4096 ];
			int r = recv( client, buffer, sizeof( buffer ), 0 );
			if ( r <= 0 )
			{
				return;
			}
			request.append( buffer, r );
		}

		const size_t header_end = request.find( "\r\n\r\n" );
		const std::string header_blob = request.substr( 0, header_end );
		std::istringstream hs( header_blob );

		std::string method;
		std::string target;
		std::string version;
		hs >> method >> target >> version;
		if ( method.empty() || target.empty() )
		{
			send_response( client, 400, make_json_error( "malformed request line" ) );
			return;
		}

		std::unordered_map<std::string, std::string> headers;
		std::string line;
		std::getline( hs, line );
		while ( std::getline( hs, line ) )
		{
			if ( !line.empty() && line.back() == '\r' )
				line.pop_back();
			size_t colon = line.find( ':' );
			if ( colon == std::string::npos )
				continue;
			std::string key = line.substr( 0, colon );
			std::string value = line.substr( colon + 1 );
			while ( !value.empty() && value.front() == ' ' )
				value.erase( value.begin() );
			headers[ key ] = value;
		}

		size_t content_length = 0;
		std::string parse_error;
		if ( !parse_content_length( headers, content_length, parse_error ) )
		{
			send_response( client, 400, make_json_error( parse_error ) );
			return;
		}

		if ( content_length > max_upload_bytes )
		{
			send_response( client, 400, make_json_error( "payload too large" ) );
			return;
		}

		const size_t total_needed = header_end + 4 + content_length;
		if ( !recv_exact( client, request, total_needed ) )
		{
			return;
		}

		std::string body = request.substr( header_end + 4, content_length );

		std::string path = target;
		std::string query;
		size_t q = target.find( '?' );
		if ( q != std::string::npos )
		{
			path = target.substr( 0, q );
			query = target.substr( q + 1 );
		}

		if ( method == "OPTIONS" )
		{
			send_response( client, 204, "", "text/plain" );
		}
		else if ( method == "GET" && path == "/health" )
		{
			send_response( client, 200, make_json_ok() );
		}
		else if ( method == "GET" && path == "/api/state" )
		{
			send_response( client, 200, make_state_json() );
		}
		else if ( method == "POST" && path == "/api/upload" )
		{
			std::string name = "uploaded.vtil";
			if ( !query.empty() )
			{
				size_t eq = query.find( "name=" );
				if ( eq != std::string::npos )
					name = url_decode( query.substr( eq + 5 ) );
			}

			std::vector<uint8_t> bytes( body.begin(), body.end() );
			const bool ok = load_routine_from_bytes( bytes, utf8_to_wide( name ) );
			if ( ok )
				send_response( client, 200, make_json_ok() );
			else
				send_response( client, 400, make_json_error( get_last_error_utf8() ) );
		}
		else
		{
			send_response( client, 404, make_json_error( "not found" ) );
		}
	}
	catch ( const std::exception& ex )
	{
		std::string msg = std::string( "handle_client exception: " ) + ex.what();
		debug_log( utf8_to_wide( msg ), backend_log_level::error );
		send_response( client, 500, make_json_error( "internal error" ) );
	}
	catch ( ... )
	{
		debug_log( L"handle_client unknown exception", backend_log_level::error );
		send_response( client, 500, make_json_error( "internal error" ) );
	}
}

int main()
{
	initialize_console();
	debug_log( L"Application startup.", backend_log_level::info );
	SetConsoleCtrlHandler( console_ctrl_handler, TRUE );

	WSADATA wsa_data;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) != 0 )
	{
		debug_log( L"WSAStartup failed.", backend_log_level::error );
		return 1;
	}

	SOCKET server = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( server == INVALID_SOCKET )
	{
		debug_log( L"Failed to create server socket.", backend_log_level::error );
		WSACleanup();
		return 1;
	}

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = htons( 8090 );

	int yes = 1;
	setsockopt( server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>( &yes ), sizeof( yes ) );

	if ( bind( server, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) == SOCKET_ERROR )
	{
		debug_log( L"bind() failed. Is port 8090 already in use?", backend_log_level::error );
		closesocket( server );
		WSACleanup();
		return 1;
	}

	if ( listen( server, 16 ) == SOCKET_ERROR )
	{
		debug_log( L"listen() failed.", backend_log_level::error );
		closesocket( server );
		WSACleanup();
		return 1;
	}

	u_long non_blocking = 1;
	ioctlsocket( server, FIONBIO, &non_blocking );

	debug_log( L"Backend listening on http://127.0.0.1:8090", backend_log_level::info );

	while ( server_running.load() )
	{
		SOCKET client = accept( server, nullptr, nullptr );
		if ( client == INVALID_SOCKET )
		{
			const int err = WSAGetLastError();
			if ( err == WSAEWOULDBLOCK || err == WSAEINTR )
			{
				Sleep( 20 );
				continue;
			}
			debug_log( utf8_to_wide( std::string( "accept() failed, code=" ) + std::to_string( err ) ), backend_log_level::warning );
			Sleep( 50 );
			continue;
		}

		if ( active_clients.load() >= max_active_clients )
		{
			debug_log( L"Client rejected: server busy", backend_log_level::warning );
			send_response( client, 503, make_json_error( "server busy" ) );
			closesocket( client );
			continue;
		}

		std::thread( [ client ]() {
			active_client_guard guard{ active_clients };
			try
			{
				handle_client( client );
			}
			catch ( const std::exception& ex )
			{
				debug_log( utf8_to_wide( std::string( "worker top-level exception: " ) + ex.what() ), backend_log_level::error );
			}
			catch ( ... )
			{
				debug_log( L"worker top-level unknown exception", backend_log_level::error );
			}
		} ).detach();
	}

	for ( int i = 0; i < 200 && active_clients.load() > 0; ++i )
		Sleep( 10 );

	closesocket( server );
	WSACleanup();
	debug_log( L"Backend shutdown complete.", backend_log_level::info );
	return 0;
}
