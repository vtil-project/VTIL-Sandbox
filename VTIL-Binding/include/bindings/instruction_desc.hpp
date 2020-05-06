#pragma once
#include <vtil/amd64>
#include <vtil/arch>
#include "basic_types.hpp"

namespace vtil::js
{
	template<>
	static JSValue as_js<operand_access>( const operand_access& value )
	{
		switch ( value )
		{
			case operand_access::read_imm:
				return "read_imm";
			case operand_access::read_reg:
				return "read_reg";
			case operand_access::read_any:
				return "read_any";
			case operand_access::write:
				return "write";
			case operand_access::readwrite:
				return "readwrite";
			default:
				return "invalid";
		}
	}

	template<>
	static JSValue as_js<const instruction_desc*>( const instruction_desc* const& desc )
	{
		JSObject output = {};
		output[ "name" ] = desc->name.data();
		output[ "is_volatile" ] = desc->is_volatile;
		output[ "memory_write" ] = desc->memory_write;
		output[ "access_size_index" ] = desc->access_size_index;
		output[ "memory_operand_index" ] = desc->memory_operand_index;
		output[ "access_types" ] = as_js_array( desc->access_types );
		output[ "branch_operands_rip" ] = as_js_array( desc->branch_operands_rip );
		output[ "branch_operands_vip" ] = as_js_array( desc->branch_operands_vip );
		return JSValue( std::move( output ) );
	}
	template<>
	static JSValue as_js<instruction_desc*>( instruction_desc* const& value ) { return as_js<const instruction_desc*>( value ); }

	template<>
	static operand_access from_js<operand_access>( const JSValue& value )
	{
		if ( !value.IsString() )
			return operand_access::invalid;

		std::wstring enum_text = from_js<std::wstring>( value[ "type" ] );
		if ( enum_text == L"read_imm" )
			return operand_access::read_imm;
		else if ( enum_text == L"read_reg" )
			return operand_access::read_reg;
		else if ( enum_text == L"read_any" )
			return operand_access::read_any;
		else if ( enum_text == L"write" )
			return operand_access::write;
		else if ( enum_text == L"readwrite" )
			return operand_access::readwrite;
		else
			return operand_access::invalid;
	}

	template<>
	static const instruction_desc* from_js<const instruction_desc*>( const JSValue& value )
	{
		std::string name = from_js<std::string>( value[ "name" ] );
		const instruction_desc* out = std::find( std::begin( instruction_list ), std::end( instruction_list ), name );
		return out != std::end( instruction_list ) ? out : nullptr;
	}
};