#pragma once
#include <vtil/amd64>
#include <vtil/arch>
#include "basic_types.hpp"
#include "operand.hpp"
#include "instruction_desc.hpp"

namespace vtil::js
{
	template<>
	static JSValue as_js<instruction>( const instruction& value )
	{
		JSObject output;
		output[ "sp_offset" ] = as_js( value.sp_offset );
		output[ "sp_index" ] = value.sp_index;
		output[ "explicit_volatile" ] = value.explicit_volatile;
		output[ "sp_reset" ] = value.sp_reset;
		output[ "vip" ] = as_js( value.vip );
		output[ "is_pseudo" ] = value.is_pseudo();
		output[ "base" ] = as_js( value.base->name );
		output[ "operands" ] = as_js_array( value.operands );
		return JSValue( std::move( output ) );
	}

	template<>
	static instruction from_js<instruction>( const JSValue& value )
	{
		instruction ins;
		ins.sp_offset = from_js<int64_t>( value[ "sp_offset" ] );
		ins.sp_index = value[ "sp_index" ];
		ins.explicit_volatile = value[ "explicit_volatile" ];
		ins.sp_reset = value[ "sp_reset" ];
		ins.vip = from_js<vip_t>( value[ "vip" ] );
		ins.base = from_js<const instruction_desc*>( value[ "base" ] );
		ins.operands = from_js_array<operand>( value[ "operands" ] );
		return ins;
	}
};