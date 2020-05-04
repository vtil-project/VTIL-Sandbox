#pragma once
#include <vtil/amd64>
#include <vtil/arch>
#include "basic_types.hpp"
#include "instruction.hpp"

namespace vtil::js
{
	template<>
	static JSValue as_js<const basic_block*>( const basic_block* const& value )
	{
		JSObject output;

		output[ "sp_offset" ] = as_js( value->sp_offset );
		output[ "sp_index" ] = value->sp_index;
		output[ "last_temporary_index" ] = value->last_temporary_index;
		output[ "entry_vip" ] = as_js( value->entry_vip );
		output[ "stream" ] = as_js_array( value->stream );

		JSArray prev_list;
		for ( auto& prev : value->prev )
			prev_list.push( prev->entry_vip );
		output[ "prev" ] = JSValue( std::move( prev_list ) );

		JSArray next_list;
		for ( auto& next : value->next )
			next_list.push( next->entry_vip );
		output[ "next" ] = JSValue( std::move( next_list ) );

		return JSValue( std::move( output ) );
	}
	template<>
	static JSValue as_js<basic_block*>( basic_block* const& value ) { return as_js<const basic_block*>( value ); }

	static basic_block* from_js( const JSValue& value, routine* rtn )
	{
		basic_block* block = new basic_block;

		block->sp_offset = from_js<int64_t>( as_js( value[ "sp_offset" ] ) );
		block->sp_index = value[ "sp_index" ];
		block->last_temporary_index = value[ "last_temporary_index" ];
		block->entry_vip = from_js<vip_t>( value[ "entry_vip" ] );
		block->stream = from_js_array<instruction, std::list<instruction>>( value[ "stream" ] );

		std::vector prev_list = from_js_array<vip_t>( value[ "prev" ] );
		for ( vip_t prev : prev_list )
			block->prev.push_back( rtn->explored_blocks[ prev ] );

		std::vector next_list = from_js_array<vip_t>( value[ "next" ] );
		for ( vip_t next : prev_list )
			block->next.push_back( rtn->explored_blocks[ next ] );

		basic_block*& entry = rtn->explored_blocks[ block->entry_vip ];
		if ( entry ) delete entry;
		return entry = block;
	}
};