#define NOMINMAX
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "json.hpp"
#include <vtil/io>
#include <vtil/vtil>

#pragma comment( lib, "ws2_32.lib" )

std::wstring file_name;
std::unique_ptr<vtil::routine> routine;
std::wstring last_error;
std::mutex state_mutex;

using json = nlohmann::json;

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

json make_json_ok() { return json{ { "ok", true } }; }

json make_json_error( const std::string& msg ) { return json{ { "ok", false }, { "error", msg } }; }

json make_json_message( const std::string& msg ) { return json{ { "ok", true }, { "message", msg } }; }

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

std::string to_lower_ascii( std::string s )
{
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return s;
}

std::string trim_ascii( const std::string& input )
{
	size_t start = 0;
	while ( start < input.size() && std::isspace( static_cast<unsigned char>( input[ start ] ) ) )
		++start;

	size_t end = input.size();
	while ( end > start && std::isspace( static_cast<unsigned char>( input[ end - 1 ] ) ) )
		--end;

	return input.substr( start, end - start );
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

json make_instruction_desc_json( const vtil::instruction_desc* desc )
{
	if ( !desc )
		return nullptr;

	json out;
	out[ "name" ] = desc->name;
	out[ "is_volatile" ] = desc->is_volatile;
	out[ "memory_write" ] = desc->memory_write;
	out[ "access_size_index" ] = desc->vaccess_size_index;
	out[ "memory_operand_index" ] = desc->memory_operand_index;
	out[ "operand_types" ] = json::array();
	for ( auto type : desc->operand_types )
		out[ "operand_types" ].push_back( operand_type_to_string( type ) );
	out[ "branch_operands_rip" ] = desc->branch_operands_rip;
	out[ "branch_operands_vip" ] = desc->branch_operands_vip;
	return out;
}

std::string format_display_instruction( const vtil::instruction& insn );

json make_operand_json( const vtil::operand& op, vtil::operand_type access_type )
{
	json out;
	out[ "type" ] = operand_type_to_string( access_type );
	out[ "text" ] = op.to_string();
	out[ "bit_count" ] = op.bit_count();
	out[ "kind" ] = op.is_register() ? "register" : "immediate";

	if ( op.is_register() )
	{
		auto reg = op.reg();
		out[ "register" ] = {
			{ "name", reg.to_string() },
			{ "combined_id", reg.combined_id },
			{ "bit_offset", reg.bit_offset },
			{ "bit_count", reg.bit_count }
		};
	}
	else
	{
		auto imm = op.imm();
		out[ "immediate" ] = {
			{ "u64", static_cast<unsigned long long>( imm.uval ) },
			{ "i64", static_cast<long long>( imm.ival ) },
			{ "bit_count", imm.bit_count }
		};
	}

	return out;
}

json make_instruction_json( const vtil::instruction& insn, size_t index )
{
	json instruction;
	instruction[ "index" ] = index;
	instruction[ "vip" ] = hex_u64( insn.vip );
	instruction[ "mnemonic" ] = insn.base ? insn.base->name : "";
	instruction[ "text" ] = insn.to_string();
	instruction[ "display_mnemonic" ] = to_upper_ascii( insn.base ? insn.base->name : "" );
	instruction[ "display_text" ] = format_display_instruction( insn );
	instruction[ "desc" ] = make_instruction_desc_json( insn.base );
	instruction[ "operand_count" ] = insn.operands.size();
	instruction[ "display_operands" ] = json::array();
	for ( const auto& operand : insn.operands )
		instruction[ "display_operands" ].push_back( operand.to_string() );
	instruction[ "operands" ] = json::array();
	for ( size_t op_i = 0; op_i < insn.operands.size(); ++op_i )
	{
		vtil::operand_type access_type = vtil::operand_type::invalid;
		if ( insn.base && op_i < insn.base->operand_types.size() )
			access_type = insn.base->operand_types[ op_i ];
		instruction[ "operands" ].push_back( make_operand_json( insn.operands[ op_i ], access_type ) );
	}
	instruction[ "is_branching" ] = insn.base && insn.base->is_branching();
	instruction[ "is_volatile" ] = insn.is_volatile();
	instruction[ "sp_offset" ] = insn.sp_offset;
	instruction[ "sp_index" ] = insn.sp_index;
	return instruction;
}

json make_block_json( vtil::vip_t vip, const vtil::basic_block& blk )
{
	json block;
	block[ "address" ] = hex_u64( vip );
	block[ "instruction_count" ] = blk.size();
	block[ "predecessors" ] = json::array();
	for ( auto* prev_blk : blk.prev )
		block[ "predecessors" ].push_back( hex_u64( prev_blk->entry_vip ) );
	block[ "successors" ] = json::array();
	for ( auto* next_blk : blk.next )
		block[ "successors" ].push_back( hex_u64( next_blk->entry_vip ) );
	block[ "instructions" ] = json::array();

	size_t index = 0;
	for ( auto it = blk.begin(); it != blk.end(); ++it, ++index )
		block[ "instructions" ].push_back( make_instruction_json( *it, index ) );
	return block;
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

json make_state_json()
{
	std::lock_guard<std::mutex> guard( state_mutex );

	json out;
	out[ "ok" ] = static_cast<bool>( routine );
	out[ "file_name" ] = wide_to_utf8( file_name );
	out[ "last_error" ] = wide_to_utf8( last_error );
	out[ "entry_point" ] = routine && routine->entry_point ? json( hex_u64( routine->entry_point->entry_vip ) ) : nullptr;
	out[ "blocks" ] = json::array();
	out[ "cfg_edges" ] = json::array();

	if ( routine )
	{
		for ( const auto& [ vip, blk ] : routine->explored_blocks )
		{
			if ( !blk ) continue;

			out[ "blocks" ].push_back( make_block_json( vip, *blk ) );
			for ( auto* next_blk : blk->next )
				out[ "cfg_edges" ].push_back( json{ { "from", hex_u64( vip ) }, { "to", hex_u64( next_blk->entry_vip ) } } );
		}
	}

	return out;
}

std::string make_operand_example_token( vtil::operand_type type, size_t index )
{
	const size_t n = index + 1;
	switch ( type )
	{
		case vtil::operand_type::read_imm:
			return n == 1 ? "0x10:64" : ( "0x" + std::to_string( n * 16 ) + ":64" );
		case vtil::operand_type::read_reg:
			return n == 1 ? "vr0" : ( "vr" + std::to_string( n - 1 ) );
		case vtil::operand_type::write:
		case vtil::operand_type::readwrite:
			return n == 1 ? "vr0" : ( "vr" + std::to_string( n - 1 ) );
		case vtil::operand_type::read_any:
			return n == 1 ? "vr0" : "0x20:64";
		default:
			return "?";
	}
}

std::string make_instruction_example_line( const vtil::instruction_desc* desc )
{
	if ( !desc )
		return "";

	std::ostringstream out;
	out << desc->name;
	if ( !desc->operand_types.empty() )
	{
		out << " ";
		for ( size_t i = 0; i < desc->operand_types.size(); ++i )
		{
			if ( i ) out << ", ";
			out << make_operand_example_token( desc->operand_types[ i ], i );
		}
	}
	return out.str();
}

json make_editor_schema_json()
{
	std::lock_guard<std::mutex> guard( state_mutex );

	std::set<std::string> registers;
	std::set<std::string> seen_mnemonics;
	std::vector<json> instruction_items;
	if ( routine )
	{
		for ( const auto& [ vip, blk ] : routine->explored_blocks )
		{
			if ( !blk ) continue;
			for ( const auto& insn : *blk )
			{
				for ( const auto& op : insn.operands )
				{
					if ( op.is_register() )
						registers.insert( op.reg().to_string() );
				}

				if ( insn.base )
				{
					const std::string mnemonic = insn.base->name;
					if ( seen_mnemonics.insert( mnemonic ).second )
					{
						json item = make_instruction_desc_json( insn.base );
						item[ "example_line" ] = make_instruction_example_line( insn.base );
						instruction_items.push_back( std::move( item ) );
					}
				}
			}
		}
	}

	std::sort( instruction_items.begin(), instruction_items.end(), []( const json& a, const json& b ) {
		return a.value( "name", std::string{} ) < b.value( "name", std::string{} );
	} );

	json out;
	out[ "ok" ] = true;
	out[ "mnemonics" ] = json::array();
	for ( const auto& item : instruction_items )
		out[ "mnemonics" ].push_back( item.value( "name", std::string{} ) );

	out[ "registers" ] = json::array();
	for ( const auto& reg_name : registers )
		out[ "registers" ].push_back( reg_name );

	out[ "instructions" ] = json::array();
	for ( auto& item : instruction_items )
		out[ "instructions" ].push_back( std::move( item ) );

	return out;
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

std::unordered_map<std::string, std::string> parse_query_pairs( const std::string& query )
{
	std::unordered_map<std::string, std::string> pairs;
	if ( query.empty() )
		return pairs;

	size_t start = 0;
	while ( start < query.size() )
	{
		size_t amp = query.find( '&', start );
		const size_t end = amp == std::string::npos ? query.size() : amp;
		const std::string item = query.substr( start, end - start );
		const size_t eq = item.find( '=' );
		if ( eq != std::string::npos )
			pairs[ url_decode( item.substr( 0, eq ) ) ] = url_decode( item.substr( eq + 1 ) );
		else
			pairs[ url_decode( item ) ] = "";

		if ( amp == std::string::npos )
			break;
		start = amp + 1;
	}
	return pairs;
}

bool parse_index( const std::unordered_map<std::string, std::string>& params, const char* key, size_t& out_value, std::string& error )
{
	auto it = params.find( key );
	if ( it == params.end() || it->second.empty() )
	{
		error = std::string( "missing query parameter: " ) + key;
		return false;
	}

	try
	{
		const unsigned long long parsed = std::stoull( it->second, nullptr, 10 );
		if ( parsed > static_cast<unsigned long long>( std::numeric_limits<size_t>::max() ) )
		{
			error = std::string( "index out of range: " ) + key;
			return false;
		}
		out_value = static_cast<size_t>( parsed );
		return true;
	}
	catch ( ... )
	{
		error = std::string( "invalid numeric query parameter: " ) + key;
		return false;
	}
}

bool parse_vip( const std::unordered_map<std::string, std::string>& params, const char* key, vtil::vip_t& out_value, std::string& error )
{
	auto it = params.find( key );
	if ( it == params.end() || it->second.empty() )
	{
		error = std::string( "missing query parameter: " ) + key;
		return false;
	}

	try
	{
		const std::string& text = it->second;
		const int base = ( text.size() > 2 && text[ 0 ] == '0' && ( text[ 1 ] == 'x' || text[ 1 ] == 'X' ) ) ? 16 : 10;
		const unsigned long long parsed = std::stoull( text, nullptr, base );
		out_value = static_cast<vtil::vip_t>( parsed );
		return true;
	}
	catch ( ... )
	{
		error = std::string( "invalid VIP query parameter: " ) + key;
		return false;
	}
}

bool parse_immediate_value( const std::string& text, bool& is_negative, uint64_t& unsigned_value, int64_t& signed_value )
{
	if ( text.empty() )
		return false;

	try
	{
		if ( text[ 0 ] == '-' )
		{
			signed_value = std::stoll( text, nullptr, 0 );
			is_negative = true;
			unsigned_value = static_cast<uint64_t>( signed_value );
			return true;
		}

		unsigned_value = std::stoull( text, nullptr, 0 );
		signed_value = static_cast<int64_t>( unsigned_value );
		is_negative = false;
		return true;
	}
	catch ( ... )
	{
		return false;
	}
}

bool validate_current_routine_roundtrip()
{
	if ( !routine )
		return false;

	std::stringstream buffer( std::ios::in | std::ios::out | std::ios::binary );
	vtil::serialize( buffer, routine.get() );
	buffer.seekg( 0 );
	vtil::routine* roundtrip_raw = nullptr;
	vtil::deserialize( buffer, roundtrip_raw );
	std::unique_ptr<vtil::routine> roundtrip( roundtrip_raw );
	return roundtrip != nullptr;
}

const vtil::instruction_desc* find_instruction_desc_by_name( const std::string& name_lower )
{
	std::lock_guard<std::mutex> guard( state_mutex );
	if ( routine )
	{
		for ( const auto& [ vip, blk ] : routine->explored_blocks )
		{
			if ( !blk )
				continue;
			for ( const auto& insn : *blk )
			{
				if ( insn.base && to_lower_ascii( insn.base->name ) == name_lower )
					return insn.base;
			}
		}
	}
	return nullptr;
}

std::string split_first_token( const std::string& line, std::string& rest )
{
	const std::string trimmed = trim_ascii( line );
	if ( trimmed.empty() )
	{
		rest.clear();
		return "";
	}

	size_t i = 0;
	while ( i < trimmed.size() && !std::isspace( static_cast<unsigned char>( trimmed[ i ] ) ) )
		++i;

	const std::string token = trimmed.substr( 0, i );
	rest = i < trimmed.size() ? trim_ascii( trimmed.substr( i ) ) : "";
	return token;
}

bool split_operands_strict( const std::string& input, std::vector<std::string>& output, std::string& error )
{
	output.clear();
	const std::string trimmed = trim_ascii( input );
	if ( trimmed.empty() )
		return true;

	std::string cur;
	for ( size_t i = 0; i < trimmed.size(); ++i )
	{
		const char c = trimmed[ i ];
		if ( c == ',' )
		{
			const std::string token = trim_ascii( cur );
			if ( token.empty() )
			{
				error = "invalid operand list: empty operand";
				return false;
			}
			output.push_back( token );
			cur.clear();
		}
		else
		{
			cur.push_back( c );
		}
	}

	const std::string tail = trim_ascii( cur );
	if ( tail.empty() )
	{
		error = "invalid operand list: trailing comma";
		return false;
	}
	output.push_back( tail );
	return true;
}

bool parse_bit_count( const std::string& text, bitcnt_t& bits )
{
	try
	{
		const unsigned long long value = std::stoull( text, nullptr, 10 );
		if ( value == 0 || value > static_cast<unsigned long long>( vtil::arch::bit_count ) )
			return false;
		bits = static_cast<bitcnt_t>( value );
		return true;
	}
	catch ( ... )
	{
		return false;
	}
}

bool parse_immediate_operand_text( const std::string& token, bitcnt_t default_bits, bool require_explicit_bits, vtil::operand& out, std::string& error )
{
	std::string value_part = token;
	bitcnt_t bit_count = default_bits;

	const size_t colon = token.rfind( ':' );
	if ( colon != std::string::npos )
	{
		value_part = trim_ascii( token.substr( 0, colon ) );
		const std::string bits_part = trim_ascii( token.substr( colon + 1 ) );
		if ( value_part.empty() || bits_part.empty() )
		{
			error = "invalid immediate format; expected <value>[:bits]";
			return false;
		}

		if ( !parse_bit_count( bits_part, bit_count ) )
		{
			error = "invalid immediate bit count";
			return false;
		}
	}
	else if ( require_explicit_bits )
	{
		error = "immediate operand requires explicit :bits suffix";
		return false;
	}

	bool is_negative = false;
	uint64_t u64 = 0;
	int64_t i64 = 0;
	if ( !parse_immediate_value( value_part, is_negative, u64, i64 ) )
	{
		error = "invalid immediate value";
		return false;
	}

	if ( is_negative )
		out = vtil::operand( i64, bit_count );
	else
		out = vtil::operand( u64, bit_count );
	return true;
}

std::unordered_map<std::string, vtil::register_desc> collect_known_registers_locked()
{
	std::unordered_map<std::string, vtil::register_desc> reg_map;
	if ( !routine )
		return reg_map;

	for ( const auto& [ vip, blk ] : routine->explored_blocks )
	{
		if ( !blk ) continue;
		for ( auto it = blk->begin(); it != blk->end(); ++it )
		{
			const auto& insn = *it;
			for ( const auto& op : insn.operands )
			{
				if ( !op.is_register() )
					continue;
				const auto name = to_lower_ascii( op.reg().to_string() );
				reg_map[ name ] = op.reg();
			}
		}
	}

	return reg_map;
}

bool parse_instruction_text_strict(
	const std::string& text,
	const vtil::instruction& old_insn,
	const std::unordered_map<std::string, vtil::register_desc>& known_registers,
	vtil::instruction& out_insn,
	std::string& error )
{
	std::string operand_tail;
	const std::string mnemonic_token = split_first_token( text, operand_tail );
	if ( mnemonic_token.empty() )
	{
		error = "instruction text is empty";
		return false;
	}

	const std::string mnemonic = to_lower_ascii( mnemonic_token );
	const vtil::instruction_desc* desc = find_instruction_desc_by_name( mnemonic );
	if ( !desc )
	{
		error = "unknown mnemonic: " + mnemonic_token;
		return false;
	}

	std::vector<std::string> operand_tokens;
	if ( !split_operands_strict( operand_tail, operand_tokens, error ) )
		return false;

	if ( operand_tokens.size() != desc->operand_count() )
	{
		error = "operand count mismatch; expected " + std::to_string( desc->operand_count() ) + ", got " + std::to_string( operand_tokens.size() );
		return false;
	}

	std::vector<vtil::operand> operands;
	operands.reserve( operand_tokens.size() );

	for ( size_t i = 0; i < operand_tokens.size(); ++i )
	{
		const auto expected_type = desc->operand_types[ i ];
		const std::string token = trim_ascii( operand_tokens[ i ] );
		const bool token_has_alpha = std::any_of( token.begin(), token.end(), []( unsigned char c ) { return std::isalpha( c ); } );

		vtil::operand parsed;
		if ( expected_type == vtil::operand_type::read_reg || expected_type == vtil::operand_type::write || expected_type == vtil::operand_type::readwrite )
		{
			const auto reg_it = known_registers.find( to_lower_ascii( token ) );
			if ( reg_it == known_registers.end() )
			{
				error = "unknown register operand at index " + std::to_string( i ) + ": " + token;
				return false;
			}
			parsed = reg_it->second;
		}
		else if ( expected_type == vtil::operand_type::read_imm )
		{
			bitcnt_t default_bits = old_insn.operands.size() > i ? old_insn.operands[ i ].bit_count() : vtil::arch::bit_count;
			if ( !parse_immediate_operand_text( token, default_bits, true, parsed, error ) )
				return false;
		}
		else if ( expected_type == vtil::operand_type::read_any )
		{
			const auto reg_it = known_registers.find( to_lower_ascii( token ) );
			if ( reg_it != known_registers.end() )
			{
				parsed = reg_it->second;
			}
			else
			{
				bitcnt_t default_bits = old_insn.operands.size() > i ? old_insn.operands[ i ].bit_count() : vtil::arch::bit_count;
				if ( token_has_alpha && token.find( "0x" ) != 0 && token.find( "-0x" ) != 0 )
				{
					error = "read_any operand is not a known register and not a valid immediate at index " + std::to_string( i );
					return false;
				}
				if ( !parse_immediate_operand_text( token, default_bits, false, parsed, error ) )
					return false;
			}
		}
		else
		{
			error = "unsupported operand type in descriptor";
			return false;
		}

		operands.push_back( parsed );
	}

	out_insn = old_insn;
	out_insn.base = desc;
	out_insn.operands = std::move( operands );
	return true;
}

bool edit_immediate_operand( vtil::vip_t block_vip, size_t instruction_index, size_t operand_index, const std::string& value_text, std::string& message_or_error )
{
	std::lock_guard<std::mutex> guard( state_mutex );

	if ( !routine )
	{
		message_or_error = "no routine loaded";
		return false;
	}

	auto blk_it = routine->explored_blocks.find( block_vip );
	if ( blk_it == routine->explored_blocks.end() || !blk_it->second )
	{
		message_or_error = "target block not found";
		return false;
	}

	vtil::basic_block* blk = blk_it->second;
	if ( instruction_index >= blk->size() )
	{
		message_or_error = "instruction index out of range";
		return false;
	}

	auto it = blk->begin();
	for ( size_t i = 0; i < instruction_index; ++i )
		++it;

	vtil::instruction& insn = *it.operator+();
	if ( operand_index >= insn.operands.size() )
	{
		message_or_error = "operand index out of range";
		return false;
	}

	auto& op = insn.operands[ operand_index ];
	if ( !op.is_immediate() )
	{
		message_or_error = "target operand is not immediate";
		return false;
	}

	bool is_negative = false;
	uint64_t new_u64 = 0;
	int64_t new_i64 = 0;
	if ( !parse_immediate_value( value_text, is_negative, new_u64, new_i64 ) )
	{
		message_or_error = "invalid immediate value";
		return false;
	}

	const auto old_imm = op.imm();
	const bool old_explicit_volatile = insn.explicit_volatile;

	if ( is_negative )
		op.imm().i64 = new_i64;
	else
		op.imm().u64 = new_u64;

	try
	{
		if ( !insn.is_valid( true ) )
		{
			op.imm() = old_imm;
			insn.explicit_volatile = old_explicit_volatile;
			message_or_error = "edit produced invalid VTIL instruction";
			return false;
		}

		if ( !validate_current_routine_roundtrip() )
		{
			op.imm() = old_imm;
			insn.explicit_volatile = old_explicit_volatile;
			message_or_error = "edited routine failed VTIL roundtrip validation";
			return false;
		}
	}
	catch ( const std::exception& ex )
	{
		op.imm() = old_imm;
		insn.explicit_volatile = old_explicit_volatile;
		message_or_error = std::string( "validation failed: " ) + ex.what();
		return false;
	}
	catch ( ... )
	{
		op.imm() = old_imm;
		insn.explicit_volatile = old_explicit_volatile;
		message_or_error = "validation failed with unknown error";
		return false;
	}

	last_error.clear();
	message_or_error = "immediate operand updated";
	return true;
}

bool export_current_routine( std::string& bytes, std::string& error )
{
	std::lock_guard<std::mutex> guard( state_mutex );
	if ( !routine )
	{
		error = "no routine loaded";
		return false;
	}

	try
	{
		std::stringstream out( std::ios::in | std::ios::out | std::ios::binary );
		vtil::serialize( out, routine.get() );
		bytes = out.str();
		if ( bytes.empty() )
		{
			error = "serialized VTIL output is empty";
			return false;
		}
		return true;
	}
	catch ( const std::exception& ex )
	{
		error = std::string( "serialize failed: " ) + ex.what();
		return false;
	}
	catch ( ... )
	{
		error = "serialize failed with unknown error";
		return false;
	}
}

bool edit_instruction_text( vtil::vip_t block_vip, size_t instruction_index, const std::string& text, std::string& message_or_error )
{
	std::lock_guard<std::mutex> guard( state_mutex );

	if ( !routine )
	{
		message_or_error = "no routine loaded";
		return false;
	}

	auto blk_it = routine->explored_blocks.find( block_vip );
	if ( blk_it == routine->explored_blocks.end() || !blk_it->second )
	{
		message_or_error = "target block not found";
		return false;
	}

	vtil::basic_block* blk = blk_it->second;
	if ( instruction_index >= blk->size() )
	{
		message_or_error = "instruction index out of range";
		return false;
	}

	auto it = blk->begin();
	for ( size_t i = 0; i < instruction_index; ++i )
		++it;

	vtil::instruction& target = *it.operator+();
	const vtil::instruction old = target;
	vtil::instruction parsed;
	const auto known_registers = collect_known_registers_locked();

	if ( !parse_instruction_text_strict( text, old, known_registers, parsed, message_or_error ) )
		return false;

	target = parsed;

	try
	{
		if ( !target.is_valid( true ) )
		{
			target = old;
			message_or_error = "edit produced invalid VTIL instruction";
			return false;
		}

		if ( !validate_current_routine_roundtrip() )
		{
			target = old;
			message_or_error = "edited routine failed VTIL roundtrip validation";
			return false;
		}
	}
	catch ( const std::exception& ex )
	{
		target = old;
		message_or_error = std::string( "validation failed: " ) + ex.what();
		return false;
	}
	catch ( ... )
	{
		target = old;
		message_or_error = "validation failed with unknown error";
		return false;
	}

	last_error.clear();
	message_or_error = "instruction updated";
	return true;
}

std::string make_download_name_utf8()
{
	std::string name = wide_to_utf8( file_name );
	if ( name.empty() )
		name = "edited.vtil";

	for ( char& c : name )
	{
		if ( c == '"' || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' )
			c = '_';
	}

	if ( name.size() < 5 || name.substr( name.size() - 5 ) != ".vtil" )
		name += ".vtil";
	return name;
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

void send_response( SOCKET sock, int status, const std::string& body, const std::string& content_type = "application/json", const std::string& extra_headers = "" )
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
	if ( content_type == "application/octet-stream" )
		headers << "Content-Type: " << content_type << "\r\n";
	else
		headers << "Content-Type: " << content_type << "; charset=utf-8\r\n";
	headers << "Access-Control-Allow-Origin: *\r\n";
	headers << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
	headers << "Access-Control-Allow-Headers: Content-Type\r\n";
	if ( !extra_headers.empty() )
		headers << extra_headers;
	headers << "Connection: close\r\n";
	headers << "Content-Length: " << body.size() << "\r\n\r\n";

	const std::string header_str = headers.str();
	send( sock, header_str.data(), static_cast<int>( header_str.size() ), 0 );
	if ( !body.empty() )
		send( sock, body.data(), static_cast<int>( body.size() ), 0 );
}

void send_response( SOCKET sock, int status, const json& body, const std::string& content_type = "application/json", const std::string& extra_headers = "" )
{
	send_response( sock, status, body.dump(), content_type, extra_headers );
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
			send_response( client, 204, std::string{}, "text/plain" );
		}
		else if ( method == "GET" && path == "/health" )
		{
			send_response( client, 200, make_json_ok() );
		}
		else if ( method == "GET" && path == "/api/state" )
		{
			send_response( client, 200, make_state_json() );
		}
		else if ( method == "GET" && path == "/api/schema" )
		{
			send_response( client, 200, make_editor_schema_json() );
		}
		else if ( method == "POST" && path == "/api/upload" )
		{
			std::string name = "uploaded.vtil";
			const auto params = parse_query_pairs( query );
			auto it_name = params.find( "name" );
			if ( it_name != params.end() && !it_name->second.empty() )
				name = it_name->second;

			std::vector<uint8_t> bytes( body.begin(), body.end() );
			const bool ok = load_routine_from_bytes( bytes, utf8_to_wide( name ) );
			if ( ok )
				send_response( client, 200, make_json_ok() );
			else
				send_response( client, 400, make_json_error( get_last_error_utf8() ) );
		}
		else if ( method == "POST" && path == "/api/edit/immediate" )
		{
			const auto params = parse_query_pairs( query );
			vtil::vip_t block_vip = 0;
			size_t instruction_index = 0;
			size_t operand_index = 0;
			std::string query_error;
			if ( !parse_vip( params, "block", block_vip, query_error ) ||
				 !parse_index( params, "instruction", instruction_index, query_error ) ||
				 !parse_index( params, "operand", operand_index, query_error ) )
			{
				send_response( client, 400, make_json_error( query_error ) );
				return;
			}

			auto value_it = params.find( "value" );
			if ( value_it == params.end() || value_it->second.empty() )
			{
				send_response( client, 400, make_json_error( "missing query parameter: value" ) );
				return;
			}

			std::string result_message;
			const bool ok = edit_immediate_operand( block_vip, instruction_index, operand_index, value_it->second, result_message );
			if ( ok )
				send_response( client, 200, make_json_message( result_message ) );
			else
				send_response( client, 400, make_json_error( result_message ) );
		}
		else if ( method == "POST" && path == "/api/edit/instruction" )
		{
			const auto params = parse_query_pairs( query );
			vtil::vip_t block_vip = 0;
			size_t instruction_index = 0;
			std::string query_error;
			if ( !parse_vip( params, "block", block_vip, query_error ) ||
				 !parse_index( params, "instruction", instruction_index, query_error ) )
			{
				send_response( client, 400, make_json_error( query_error ) );
				return;
			}

			const std::string text = trim_ascii( body );
			if ( text.empty() )
			{
				send_response( client, 400, make_json_error( "missing instruction text in request body" ) );
				return;
			}

			std::string result_message;
			const bool ok = edit_instruction_text( block_vip, instruction_index, text, result_message );
			if ( ok )
				send_response( client, 200, make_json_message( result_message ) );
			else
				send_response( client, 400, make_json_error( result_message ) );
		}
		else if ( method == "GET" && path == "/api/download" )
		{
			std::string output;
			std::string error;
			if ( !export_current_routine( output, error ) )
			{
				send_response( client, 400, make_json_error( error ) );
				return;
			}

			const std::string download_name = make_download_name_utf8();
			const std::string disposition = "Content-Disposition: attachment; filename=\"" + download_name + "\"\r\n";
			send_response( client, 200, output, "application/octet-stream", disposition );
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
