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

#ifndef AMHPROTOCOL_H
#define AMHPROTOCOL_H

//
// CAMHProtocol
//

#define AMH_HEADER_CONSTANT		252

class CAMHPong;

class CAMHProtocol
{
public:
	enum Protocol {
		AMH_INIT = 1,
		AMH_PING = 2,
		AMH_PONG = 3
	};

	CAMHProtocol( );
	~CAMHProtocol( );

	// receive functions
	CAMHPong *RECEIVE_AMH_PONG( BYTEARRAY data );

	// send functions
	BYTEARRAY SEND_AMH_INIT( );
	BYTEARRAY SEND_AMH_PING( BYTEARRAY ping );

	// other functions

private:
	bool AssignLength( BYTEARRAY &content );
	bool ValidateLength( BYTEARRAY &content );
};

//
// CAMHPong
//
	
class CAMHPong
{
private:
	uint32_t m_Version;
	BYTEARRAY m_Pong;

public:
	CAMHPong( uint32_t nVersion, BYTEARRAY nPong );
	~CAMHPong( );

	uint32_t GetVersion( ) { return m_Version; }
	BYTEARRAY GetPong( ) { return m_Pong; }
};

#endif
