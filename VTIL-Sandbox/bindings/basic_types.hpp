#pragma once
#include <AppCore/JSHelpers.h>
#include <type_traits>

namespace vtil::js
{
	using namespace ultralight;

	template<typename T>
	static JSValue as_js( const T& value ) 
	{
		if constexpr ( std::is_same_v<T, uint64_t> ||
					   std::is_same_v<T, int64_t> )
			return as_js( std::to_wstring( value ) );
		if constexpr ( std::is_same_v<T, std::string> ||
					   std::is_same_v<T, std::wstring> )
			return as_js( String{ value.data(), value.length() } );
		else
			return JSValue( value ); 
	}


	template<typename T>
	static T from_js( const JSValue& value ) 
	{
		if constexpr ( std::is_same_v<T, uint64_t> )
		{
			if ( value.IsString() )
				return wcstoull( String( value.ToString() ).utf16().data(), 0, 10 );
		}
		else if constexpr ( std::is_same_v<T, int64_t> )
		{
			if ( value.IsString() )
				return wcstoll( String( value.ToString() ).utf16().data(), 0, 10 );
		}
		else if constexpr ( std::is_same_v<T, std::string> ||
						    std::is_same_v<T, std::wstring> )
		{
			String str = value.ToString();
			return T( str.utf16().data(), str.utf16().data() + str.utf16().length() );
		}
		else
		{
			return T( value );
		}
	}

	template<typename T>
	static JSValue as_js_array( const T& iterable )
	{
		JSArray output;
		for ( auto& entry : iterable )
			output.push( as_js( entry ) );
		return JSValue( std::move( output ) );
	}

	template<typename T, typename container_type = std::vector<T>>
	static container_type from_js_array( const JSValue& src )
	{
		if ( !src.IsArray() ) return {};
		JSArray array = src.ToArray();

		container_type output;
		for ( size_t i = 0; i < array.length(); i++ )
			output.push_back( from_js<T>( array[ i ] ) );
		return output;
	}
};