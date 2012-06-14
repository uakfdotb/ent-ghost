/*

   Copyright 2010 Trevor Hogan

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#ifndef GCBIPROTOCOL_H
#define GCBIPROTOCOL_H

//
// CGCBIProtocol
//

#define GCBI_HEADER_CONSTANT		249

#define REJECTGCBI_INVALID			1
#define REJECTGCBI_NOTFOUND			2

class CIncomingGarenaUser;

class CGCBIProtocol
{
public:
	enum Protocol {
		GCBI_INIT				= 1
	};

	CGCBIProtocol( );
	~CGCBIProtocol( );

	// receive functions
	CIncomingGarenaUser *RECEIVE_GCBI_INIT( BYTEARRAY data );

	// send functions

	// other functions

private:
	bool AssignLength( BYTEARRAY &content );
	bool ValidateLength( BYTEARRAY &content );
};

//
// CIncomingGarenaUser
//
	
class CIncomingGarenaUser
{
private:
	uint32_t m_IP;
	uint32_t m_UserID;
	uint32_t m_RoomID;
	uint32_t m_UserExp;
	string m_CountryCode;

public:
	CIncomingGarenaUser( uint32_t nIP, uint32_t nUserID, uint32_t nRoomID, uint32_t nUserExp, string nCountryCode );
	~CIncomingGarenaUser( );

	uint32_t GetIP( ) { return m_IP; }
	uint32_t GetUserID( ) { return m_UserID; }
	uint32_t GetRoomID( ) { return m_RoomID; }
	uint32_t GetUserExp( ) { return m_UserExp; }
	string GetCountryCode( ) { return m_CountryCode; }
};

#endif
