/*

	ent-ghost
	Copyright [2011-2013] [Jack Lu]

	This file is part of the ent-ghost source code.

	ent-ghost is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	ent-ghost source code is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with ent-ghost source code. If not, see <http://www.gnu.org/licenses/>.

	ent-ghost is modified from GHost++ (http://ghostplusplus.googlecode.com/)
	GHost++ is Copyright [2008] [Trevor Hogan]

*/

#include "ghost.h"
#include "util.h"
#include "amhprotocol.h"

//
// CAMHProtocol
//

CAMHProtocol :: CAMHProtocol( )
{

}

CAMHProtocol :: ~CAMHProtocol( )
{

}

///////////////////////
// RECEIVE FUNCTIONS //
///////////////////////

CAMHPong *CAMHProtocol :: RECEIVE_AMH_PONG( BYTEARRAY data )
{
	// 2 bytes					-> Header
	// 2 bytes					-> Length
	// 4 bytes					-> Version (little endian)
	// 32 bytes					-> Pong data

	if( ValidateLength( data ) && data.size( ) == 40 )
	{
		BYTEARRAY Version = BYTEARRAY( data.begin( ) + 4, data.begin( ) + 8 );
		BYTEARRAY Pong = BYTEARRAY( data.begin( ) + 8, data.begin( ) + 40 );
		
		return new CAMHPong(UTIL_ByteArrayToUInt32( Version, true ), Pong );
	}

	return NULL;
}

////////////////////
// SEND FUNCTIONS //
////////////////////

BYTEARRAY CAMHProtocol :: SEND_AMH_INIT( )
{
	BYTEARRAY packet;
	packet.push_back( AMH_HEADER_CONSTANT );				// AMH header constant
	packet.push_back( AMH_INIT );							// AMH_INIT
	packet.push_back( 0 );									// packet length will be assigned later
	packet.push_back( 0 );									// packet length will be assigned later
	AssignLength( packet );
	
	return packet;
}

BYTEARRAY CAMHProtocol :: SEND_AMH_PING( BYTEARRAY ping )
{
	BYTEARRAY packet;
	packet.push_back( AMH_HEADER_CONSTANT );				// AMH header constant
	packet.push_back( AMH_PING );							// AMH_PING
	packet.push_back( 0 );									// packet length will be assigned later
	packet.push_back( 0 );									// packet length will be assigned later
	UTIL_AppendByteArrayFast( packet, ping );				// ping data
	AssignLength( packet );
	
	return packet;
}

/////////////////////
// OTHER FUNCTIONS //
/////////////////////

bool CAMHProtocol :: AssignLength( BYTEARRAY &content )
{
	// insert the actual length of the content array into bytes 3 and 4 (indices 2 and 3)

	BYTEARRAY LengthBytes;

	if( content.size( ) >= 4 && content.size( ) <= 65535 )
	{
		LengthBytes = UTIL_CreateByteArray( (uint16_t)content.size( ), false );
		content[2] = LengthBytes[0];
		content[3] = LengthBytes[1];
		return true;
	}

	return false;
}

bool CAMHProtocol :: ValidateLength( BYTEARRAY &content )
{
	// verify that bytes 3 and 4 (indices 2 and 3) of the content array describe the length

	uint16_t Length;
	BYTEARRAY LengthBytes;

	if( content.size( ) >= 4 && content.size( ) <= 65535 )
	{
		LengthBytes.push_back( content[2] );
		LengthBytes.push_back( content[3] );
		Length = UTIL_ByteArrayToUInt16( LengthBytes, false );

		if( Length == content.size( ) )
			return true;
	}

	return false;
}

//
// CAMHPong
//

CAMHPong :: CAMHPong( uint32_t nVersion, BYTEARRAY nPong ) : m_Version( nVersion ), m_Pong( nPong )
{

}

CAMHPong :: ~CAMHPong( )
{

}
