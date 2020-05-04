#pragma once
#include <vtil/amd64>
#include <vtil/arch>
#include "basic_types.hpp"

namespace vtil::js
{
	template<>
	static JSValue as_js<operand>( const operand& value )
	{
		JSObject output;

		output[ "type" ] = value.is_register() ? "reg" : "imm";
		output[ "text" ] = value.to_string().data();
		output[ "bit_count" ] = value.is_register() ? value.reg.bit_count : value.imm.bit_count;
		output[ "bit_offset" ] = value.is_register() ? value.reg.bit_offset : 0;
		
		if ( value.is_register() )
		{
			output[ "local_id" ] = as_js( value.reg.local_id );
			output[ "is_physical" ] = value.reg.is_physical();
			output[ "is_local" ] = value.reg.is_local();
			output[ "is_flags" ] = value.reg.is_flags();
			output[ "is_stack_pointer" ] = value.reg.is_stack_pointer();
			output[ "is_volatile" ] = value.reg.is_volatile();
			output[ "is_read_only" ] = value.reg.is_read_only();
		}
		else
		{
			output[ "i64" ] = as_js( value.imm.i64 );
		}

		return JSValue( std::move( output ) );
	}

	template<>
	static operand from_js<operand>( const JSValue& value )
	{
		bitcnt_t bit_count = value[ "bit_count" ];
		bitcnt_t bit_offset = value[ "bit_offset" ];
		std::wstring type_name = from_js<std::wstring>( value[ "type" ] );
		
		if ( type_name == L"reg" )
		{
			uint8_t flags = 0;
			if ( bool( value[ "is_physical" ] ) ) flags |= register_physical;
			if ( bool( value[ "is_local" ] ) ) flags |= register_local;
			if ( bool( value[ "is_flags" ] ) ) flags |= register_flags;
			if ( bool( value[ "is_stack_pointer" ] ) ) flags |= register_stack_pointer;
			if ( bool( value[ "is_volatile" ] ) ) flags |= register_volatile;
			if ( bool( value[ "is_read_only" ] ) ) flags |= register_readonly;
			return register_desc{ flags, from_js<size_t>( value[ "local_id" ] ), bit_count, bit_offset };
		}
		else if( type_name == L"imm" )
		{
			return { from_js<int64_t>( value[ "i64" ] ), bit_count };
		}
		return {};
	}
};