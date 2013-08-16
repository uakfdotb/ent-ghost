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
#include "language.h"
#include "socket.h"
#include "commandpacket.h"
#include "bnet.h"
#include "map.h"
#include "gameplayer.h"
#include "gameprotocol.h"
#include "gpsprotocol.h"
#include "game_base.h"
#include "gameslot.h"
#include "ghostdb.h"
#include "stageplayer.h"

//
// CStagePlayer
//

CStagePlayer :: CStagePlayer( CGameProtocol *nProtocol, CTCPSocket *nSocket, CGHost *nGHost ) : m_Protocol( nProtocol ), m_Socket( nSocket ), m_GHost( nGHost ), m_ConnectCheck( NULL ), m_Checked( false ), m_DeleteMe( false ), m_ConnectionTicks( GetTicks( ) ), m_LoadingStage( 0 ), m_LastStageTicks( GetTicks( ) ), m_MapOK( false ), m_LastPingTime( GetTime( ) ), m_Score( -10000.0 ), m_JoinTicks( 0 ), m_SpoofSent( false ), m_ScoreRange( 100.0 ), m_LastScoreRangeIncrease( GetTime( ) ), m_ScoreCheck( NULL ), m_MuteTicks( 0 )
{
	if( nSocket )
		m_CachedIP = nSocket->GetIPString( );
	else
		m_CachedIP = string( );
	
	m_PID = rand( ) % 12 + 1; // use PID from 1 to 10
}

CStagePlayer :: ~CStagePlayer( )
{
	if( m_Socket )
		delete m_Socket;

	while( !m_Packets.empty( ) )
	{
		delete m_Packets.front( );
		m_Packets.pop( );
	}
}

BYTEARRAY CStagePlayer :: GetExternalIP( )
{
	unsigned char Zeros[] = { 0, 0, 0, 0 };

	if( m_Socket ) {
		return m_Socket->GetIP( );
	}

	return UTIL_CreateByteArray( Zeros, 4 );
}

string CStagePlayer :: GetExternalIPString( )
{
	if( m_Socket ) {
		string IPString = m_Socket->GetIPString( );
		
		if( !IPString.empty( ) && IPString != "0.0.0.0" )
			return m_Socket->GetIPString( );
		else
			return m_CachedIP;
	}

	return m_CachedIP;
}

bool CStagePlayer :: Update( void *fd )
{
	if( !m_Socket )
		return false;
	
	if( m_LoadingStage == 0 )
	{
		m_LoadingStage = 1;
		m_LastStageTicks = GetTicks( );
	}
	
	else if( m_LoadingStage == 1 )
	{
		vector<CGameSlot> Slots = m_GHost->m_Map->GetSlots( );
		Slots[0] = CGameSlot( m_PID, 100, SLOTSTATUS_OCCUPIED, 0, Slots[0].GetTeam( ), Slots[0].GetColour( ), Slots[0].GetRace( ) );
		Send( m_Protocol->SEND_W3GS_SLOTINFOJOIN( m_PID, m_Socket->GetPort( ), GetExternalIP( ), Slots, 100, m_GHost->m_Map->GetMapLayoutStyle( ), m_GHost->m_Map->GetMapNumPlayers( ) ) );
		
		// add virtual player
		m_VirtualPID = 1;
		m_ChatPID = 2;
		
		if( m_PID == 1 )
		{
			m_VirtualPID = 2;
			m_ChatPID = 3;
		}
		else if( m_PID == 2 )
		{
			m_VirtualPID = 1;
			m_ChatPID = 3;
		}
		
		BYTEARRAY IP;
		IP.push_back( 0 );
		IP.push_back( 0 );
		IP.push_back( 0 );
		IP.push_back( 0 );
		Send( m_Protocol->SEND_W3GS_PLAYERINFO( m_VirtualPID, "ENT", IP, IP ) );
		Send( m_Protocol->SEND_W3GS_PLAYERINFO( m_ChatPID, ":", IP, IP ) );
		
		// map check
		Send( m_Protocol->SEND_W3GS_MAPCHECK( m_GHost->m_Map->GetMapPath( ), m_GHost->m_Map->GetMapSize( ), m_GHost->m_Map->GetMapInfo( ), m_GHost->m_Map->GetMapCRC( ), m_GHost->m_Map->GetMapSHA1( ) ) );

		m_LoadingStage = 2;
		CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Game initialization packets sent, waiting for player response." );
		SendChat( "[System] Total players waiting for a game: " + UTIL_ToString( m_GHost->CountStagePlayers( ) ) );
		SendChat( "******** Welcome! Please wait while staging checks are completed." );
		
		m_LastStageTicks = GetTicks( );
	}
	
	else if( m_LoadingStage == 2 && m_MapOK && GetTicks( ) - m_LastStageTicks > 5000 && m_Checked && m_Score != -10000.0 )
	{
		m_LoadingStage = 3;
		m_LastStageTicks = GetTicks( );
		CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Moved to stage three, waiting for game!" );
		
		// since we don't use modes anymore, this is the same as the next loading stage
	}
	
	else if( m_LoadingStage == 3 )
	{
		m_LoadingStage = 4;
		m_LastStageTicks = GetTicks( );
		SendChat( "[System] Looks like everything is good, attempting to assign you to a game now... (this could take a few minutes)" );
		SendChat( "[System] Use !status to check the current status." );
	}

	else if( m_LoadingStage == 9 && GetTicks( ) - m_LastStageTicks > 10000 )
		m_DeleteMe = true;

	//network related operations
	
	m_Socket->DoRecv( (fd_set *)fd );
	ExtractPackets( );
	ProcessPackets( );
	
	// send score check if needed
	
	if( !m_ScoreCheck && m_Score == -10000.0 && m_Checked )
		m_ScoreCheck = m_GHost->m_DB->ThreadedScoreCheck( m_GHost->m_Map->GetMapMatchMakingCategory( ), m_Name, m_Realm );
	
	// process score check
	
	if( m_ScoreCheck && m_ScoreCheck->GetReady( ) )
	{
		if( m_ScoreCheck->GetResult( ) != NULL )
			m_Score = m_ScoreCheck->GetResult( )[1];
		else
			m_Score = 1000.0;
		
		// increase initial score range if we have higher score
		if( m_Score > 1600 )
			m_ScoreRange = 200.0;
		else if( m_Score > 1300 )
			m_ScoreRange = 150.0;
		
		SendChat( "[System] Initial search range set to " + UTIL_ToString( m_Score, 1 ) + " +/- " + UTIL_ToString( m_ScoreRange, 1 ) );
		
		m_GHost->m_DB->RecoverCallable( m_ScoreCheck );
		delete m_ScoreCheck;
		m_ScoreCheck = NULL;
	}
	
	// process ent connect check
	
	if( m_ConnectCheck && m_ConnectCheck->GetReady( ) )
	{
		if( m_ConnectCheck->GetResult( ) )
		{
			m_Checked = true;
			CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Authentication successful" );
			SendChat( "[System] EC client check successful!" );
		}
		else
		{
			CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Terminating: EC check failure" );
			m_DeleteMe = true;
		}

		m_GHost->m_DB->RecoverCallable( m_ConnectCheck );
		delete m_ConnectCheck;
		m_ConnectCheck= NULL;
	}
	
	// unmute players
	
	if( m_MuteTicks != 0 && GetTicks( ) - m_MuteTicks > 15000 )
	{
		m_MuteTicks = 0;
	}
	
	// send spoof check
	
	if( !m_Checked && !m_SpoofSent && GetTicks( ) - m_JoinTicks > 4000 )
	{
		m_SpoofSent = true;
		
		for( vector<CBNET *> :: iterator i = m_GHost->m_BNETs.begin( ); i != m_GHost->m_BNETs.end( ); i++ )
		{
			if( (*i)->GetServer( ) == m_Realm )
				(*i)->QueueChatCommand( "/whois " + m_Name );
		}
	}
	
	// make sure we don't keep this socket open forever (disconnect after forty seconds)
	
	if( m_LoadingStage < 3 && GetTicks( ) - m_ConnectionTicks > 40000 )
	{
		CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Terminating: user not ready after forty seconds." );
		m_DeleteMe = true;
	}
	
	// increase our acceptable score range
	
	if( GetTime( ) - m_LastScoreRangeIncrease > 30 && m_LoadingStage == 4 )
	{
		m_LastScoreRangeIncrease = GetTime( );
		uint32_t MaxRange = 250;
		
		if( m_Score > 1200 ) MaxRange = 300;
		if( m_Score > 1400 ) MaxRange = 350;
		if( m_Score > 1600 ) MaxRange = 400;
		
		if( m_ScoreRange < MaxRange )
		{
			m_ScoreRange += 50;
			SendChat( "[System] Expanding search range to " + UTIL_ToString( m_Score, 1 ) + " +/- " + UTIL_ToString( m_ScoreRange, 1 ) );
		}
		else
			SendChat( "[System] Current search range set to " + UTIL_ToString( m_Score, 1 ) + " +/- " + UTIL_ToString( m_ScoreRange, 1 ) );
	}
	
	// socket timeouts
	
	if( m_Socket && GetTime( ) - m_Socket->GetLastRecv( ) >= 30 )
	{
		CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Terminating: socket timeout" );
		m_DeleteMe = true;
	}
	
	// ping
	
	if( GetTime( ) - m_LastPingTime >= 5 )
	{
		Send( m_Protocol->SEND_W3GS_PING_FROM_HOST( ) );
		m_LastPingTime = GetTime( );
	}

	return m_Socket->HasError( ) || !m_Socket->GetConnected( ) || m_DeleteMe;
}

void CStagePlayer :: ExtractPackets( )
{
	if( !m_Socket )
		return;

	// extract as many packets as possible from the socket's receive buffer and put them in the m_Packets queue

	string *RecvBuffer = m_Socket->GetBytes( );
	BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

	// a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

	while( Bytes.size( ) >= 4 )
	{
		if( Bytes[0] == W3GS_HEADER_CONSTANT || Bytes[0] == GPS_HEADER_CONSTANT )
		{
			// bytes 2 and 3 contain the length of the packet

			uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

			if( Length >= 4 )
			{
				if( Bytes.size( ) >= Length )
				{
					m_Packets.push( new CCommandPacket( Bytes[0], Bytes[1], BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length ) ) );
					*RecvBuffer = RecvBuffer->substr( Length );
					Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
				}
				else
					return;
			}
			else
			{
				m_DeleteMe = true;
				return;
			}
		}
		else
		{
			m_DeleteMe = true;
			return;
		}
	}
}

void CStagePlayer :: ProcessPackets( )
{
	if( !m_Socket )
		return;

	// process all the received packets in the m_Packets queue
	
	CIncomingChatPlayer *ChatPlayer = NULL;
	CIncomingJoinPlayer *IncomingPlayer = NULL;
	CIncomingMapSize *MapSize = NULL;

	while( !m_Packets.empty( ) )
	{
		CCommandPacket *Packet = m_Packets.front( );
		m_Packets.pop( );

		if( Packet->GetPacketType( ) == W3GS_HEADER_CONSTANT )
		{
			switch( Packet->GetID( ) )
			{
			case CGameProtocol :: W3GS_REQJOIN:
				IncomingPlayer = m_Protocol->RECEIVE_W3GS_REQJOIN( Packet->GetData( ) );

				if( IncomingPlayer && m_JoinTicks == 0 )
				{
					if( IncomingPlayer->GetName( ).empty( ) || IncomingPlayer->GetName( ).length( ) > 15 )
					{
						CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "] Error: invalid name [" + IncomingPlayer->GetName( ) + "]" );
						m_DeleteMe = true;
					}
					else
					{
						m_Name = IncomingPlayer->GetName( );
						m_JoinTicks = GetTicks( );
						uint32_t HostCounterID = IncomingPlayer->GetHostCounter( ) >> 28;
					
						if( HostCounterID == 15 )
						{
							m_Realm = "entconnect";
							m_ConnectCheck = m_GHost->m_DB->ThreadedConnectCheck( IncomingPlayer->GetName( ), IncomingPlayer->GetEntryKey( ) );
							m_SpoofSent = true;
						}
						else
						{
							for( vector<CBNET *> :: iterator i = m_GHost->m_BNETs.begin( ); i != m_GHost->m_BNETs.end( ); ++i )
							{
								if( (*i)->GetHostCounterID( ) == HostCounterID )
									m_Realm = (*i)->GetServer( );
							}
						}
					
						if( !m_DeleteMe )
							CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "] Attempting to authenticate as [" + m_Name + "@" + m_Realm + "]" );
					}
				}

				delete IncomingPlayer;
				IncomingPlayer = NULL;
				break;
			
			case CGameProtocol :: W3GS_MAPSIZE:
				MapSize = m_Protocol->RECEIVE_W3GS_MAPSIZE( Packet->GetData( ), m_GHost->m_Map->GetMapSize( ) );

				if( MapSize )
				{
					// player must have map already, otherwise kick
					if( MapSize->GetSizeFlag( ) == 1 && MapSize->GetMapSize( ) == UTIL_ByteArrayToUInt32( m_GHost->m_Map->GetMapSize( ), false ) )
					{
						m_MapOK = true;
						SendChat( "[System] Confirmed that you have the map downloaded." );
						CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] User has confirmed map OK." );
					}
					else
					{
						m_LoadingStage = 9;
						m_LastStageTicks = GetTicks( );
						SendChat( "Error: please download the map first!" );
						CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Error: user does not have the map!" );
					}
				}

				delete MapSize;
				MapSize = NULL;
				break;

			case CGameProtocol :: W3GS_CHAT_TO_HOST:
				ChatPlayer = m_Protocol->RECEIVE_W3GS_CHAT_TO_HOST( Packet->GetData( ) );
				
				if( ChatPlayer )
				{
					if( ChatPlayer->GetType( ) == CIncomingChatPlayer :: CTH_MESSAGE )
					{
						string Message = ChatPlayer->GetMessage( );
						
						if( !Message.empty( ) && Message[0] == '!' )
						{
							transform( Message.begin( ), Message.end( ), Message.begin( ), (int(*)(int))tolower );
							string Command = Message.substr( 1 );
							
							if( Command == "status" )
							{
								SendChat( "[System] Total players waiting for a game: " + UTIL_ToString( m_GHost->CountStagePlayers( ) ) );
								SendChat( "[System] Current search range set to " + UTIL_ToString( m_Score, 1 ) + " +/- " + UTIL_ToString( m_ScoreRange, 1 ) );
							}
							else if( Command == "time" )
							{
								SendChat( "[System] You have been waiting for " + UTIL_ToString( ( GetTicks( ) - m_ConnectionTicks ) / 60000 ) + " minutes." );
							}
						}
						else if( !Message.empty( ) )
						{
							m_MuteMessages.push_back( GetTicks( ) );

							if( m_MuteMessages.size( ) > 7 )
								m_MuteMessages.erase( m_MuteMessages.begin( ) );

							uint32_t RecentCount = 0;

							for( unsigned int i = 0; i < m_MuteMessages.size( ); ++i )
							{
								if( GetTicks( ) - m_MuteMessages[i] < 7000 )
									RecentCount++;
							}

							if( m_MuteTicks == 0 && RecentCount >= 7 )
							{
								m_MuteTicks = GetTicks( );
								SendChat( "You have been automatically muted for spamming. (You will be unmuted momentarily, but please do not spam again!)" );
								m_MuteMessages.clear( );
							}
							
							if( m_MuteTicks == 0 )
							{
								string Realm = m_Realm;
							
								if( Realm == "uswest.battle.net" ) Realm = "USWest";
								else if( Realm == "useast.battle.net" ) Realm = "USEast";
								else if( Realm == "europe.battle.net" ) Realm = "Europe";
								else if( Realm == "asia.battle.net" ) Realm = "Asia";
							
								m_GHost->BroadcastChat( m_Name + "@" + Realm, Message );
							}
						}
					}
				}

				delete ChatPlayer;
				ChatPlayer = NULL;
				break;

			case CGameProtocol :: W3GS_LEAVEGAME:
				CONSOLE_Print( "[STAGE " + GetExternalIPString( ) + "/" + m_Name + "@" + m_Realm + "] Terminating: player left voluntarily." );
				m_DeleteMe = true;
				break;
			}
		}

		delete Packet;
	}
}

void CStagePlayer :: Send( BYTEARRAY data )
{
	if( m_Socket )
		m_Socket->PutBytes( data );
}

void CStagePlayer :: SendChat( string message, bool local )
{
	if( local )
		Send( m_Protocol->SEND_W3GS_CHAT_FROM_HOST( m_VirtualPID, UTIL_CreateByteArray( m_PID ), 16, BYTEARRAY( ), message ) );
	else
		Send( m_Protocol->SEND_W3GS_CHAT_FROM_HOST( m_ChatPID, UTIL_CreateByteArray( m_PID ), 16, BYTEARRAY( ), message ) );
}

bool CStagePlayer :: IsScoreAcceptable( double nScore, int nScoreType )
{
	if( nScore > m_Score - m_ScoreRange && nScore < m_Score + m_ScoreRange )
		return true;
	else if( nScoreType == -1 && m_Score - m_ScoreRange < nScore )
		return true;
	else if( nScoreType == 1 && m_Score + m_ScoreRange > nScore )
		return true;
	else
		return false;
}

void CStagePlayer :: RemoveVirtual( )
{
	Send( m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS( m_VirtualPID, PLAYERLEAVE_LOBBY ) );
	Send( m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS( m_ChatPID, PLAYERLEAVE_LOBBY ) );
}
