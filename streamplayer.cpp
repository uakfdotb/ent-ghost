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
#include "streamplayer.h"

//
// CStreamPlayer
//

CStreamPlayer :: CStreamPlayer( CGameProtocol *nProtocol, CTCPSocket *nSocket, CGHost *nGHost ) : m_Protocol( nProtocol ), m_Game( NULL ), m_Socket( nSocket ), m_GHost( nGHost ), m_ConnectCheck( NULL ), m_Checked( false ), m_DeleteMe( false ), m_ConnectionTicks( GetTicks( ) ), m_StreamPosition( 0 ), m_ActionsIndex( 0 ), m_LoadingStage( 0 ), m_LastStageTicks( GetTicks( ) ), m_MapOK( false ), m_FinishedLoading( false ), m_LastPingTime( GetTime( ) ), m_BeganStreaming( false )
{
	if( nSocket )
		m_CachedIP = nSocket->GetIPString( );
	else
		m_CachedIP = string( );
}

CStreamPlayer :: ~CStreamPlayer( )
{
	if( m_Socket )
		delete m_Socket;

	while( !m_Packets.empty( ) )
	{
		delete m_Packets.front( );
		m_Packets.pop( );
	}
}

BYTEARRAY CStreamPlayer :: GetExternalIP( )
{
	unsigned char Zeros[] = { 0, 0, 0, 0 };

	if( m_Socket ) {
		return m_Socket->GetIP( );
	}

	return UTIL_CreateByteArray( Zeros, 4 );
}

string CStreamPlayer :: GetExternalIPString( )
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

bool CStreamPlayer :: Update( void *fd )
{
	if( !m_Socket )
		return false;
	
	if( m_Game )
	{
		// if m_Game is set here, then it indicates that we are in the game's thread
		// we can't place this code under ProcessPackets, because ProcessPackets also sets m_Game
		
		if( m_LoadingStage == 0 && m_Checked )
		{
			m_LoadingStage = 1;
			m_LastStageTicks = GetTicks( );
		}
		
		else if( m_LoadingStage == 1 )
		{
			string FailMessage = "";
			
			// update game slots download status for the player's PID
			vector<CGameSlot> Slots = m_Game->m_Slots;
			
			// first, make sure that we are able to stream this game (need to have five minutes elapsed and replay is enabled)
			if( !m_Game->m_StreamPackets )
				FailMessage = "Streaming is not enabled in this game (probably no observer slot with fake player exists).";
			else if( m_Game->GetGameTicks( ) < m_GHost->m_StreamLimit || !m_Game->GetGameLoaded( ) )
				FailMessage = "The in-game time has not exceeded " + UTIL_ToString( m_GHost->m_StreamLimit ) + " seconds, please try streaming at a later time.";
			else if( m_Game->GetClosed( ) )
				FailMessage = "This game has already ended, you should be able to view the replay instead.";
			else
			{
				// find open observer slot
				unsigned char StreamPID = m_Game->GetStreamPID( );
				unsigned char StreamSID = m_Game->GetStreamSID( );
				
				if( StreamPID != 255 && StreamSID < Slots.size( ) )
				{
					Slots[StreamSID].SetDownloadStatus( 255 );
					Send( m_Protocol->SEND_W3GS_SLOTINFOJOIN( StreamPID, m_Socket->GetPort( ), GetExternalIP( ), Slots, m_Game->GetRandomSeed( ), m_Game->GetStreamMapLayoutStyle( ), m_Game->GetStreamMapNumPlayers( ) ) );
					
					BYTEARRAY BlankIP;
					BlankIP.push_back( 0 );
					BlankIP.push_back( 0 );
					BlankIP.push_back( 0 );
					BlankIP.push_back( 0 );
					
					// fake player information
					for( vector<FakePlayer> :: iterator i = m_Game->m_FakePlayers.begin( ); i != m_Game->m_FakePlayers.end( ); ++i )
					{
						if( i->pid != StreamPID )
							Send( m_Protocol->SEND_W3GS_PLAYERINFO( i->pid, i->name, BlankIP, BlankIP ) );
					}
					
					// player information
					for( vector<CGamePlayer *> :: iterator i = m_Game->m_Players.begin( ); i != m_Game->m_Players.end( ); ++i )
					{
						if( !(*i)->GetLeftMessageSent( ) && (*i)->GetPID( ) != StreamPID )
							Send( m_Protocol->SEND_W3GS_PLAYERINFO( (*i)->GetPID( ), (*i)->GetFriendlyName( ), BlankIP, BlankIP ) );
					}
					
					// map check
					Send( m_Game->GetCachedMapCheck( ) );
				}
				else
					FailMessage = "This game does not support streaming (no slots for streaming found).";
			}
			
			if( !FailMessage.empty( ) )
			{
				//send fake join packets and then show the message
				//first update download status of slot with PID=2
				
				for( unsigned char i = 0; i < Slots.size( ); i++ )
				{
					if( Slots[i].GetPID( ) == 2 )
						Slots[i].SetDownloadStatus( 255 );
				}
				
				Send( m_Protocol->SEND_W3GS_SLOTINFOJOIN( 2, m_Socket->GetPort( ), GetExternalIP( ), Slots, m_Game->GetRandomSeed( ), m_Game->GetStreamMapLayoutStyle( ), m_Game->GetStreamMapNumPlayers( ) ) );
			
				BYTEARRAY BlankIP;
				BlankIP.push_back( 0 );
				BlankIP.push_back( 0 );
				BlankIP.push_back( 0 );
				BlankIP.push_back( 0 );
				Send( m_Protocol->SEND_W3GS_PLAYERINFO( 1, m_Game->GetVirtualHostName( ), BlankIP, BlankIP ) );
				Send( m_Game->GetCachedMapCheck( ) );
				GetSocket( )->PutBytes( m_Protocol->SEND_W3GS_CHAT_FROM_HOST( 1, UTIL_CreateByteArray( 2 ), 16, BYTEARRAY( ), FailMessage ) );
				
				CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Stream failure: " + FailMessage );
				m_LoadingStage = 9; //terminate
			}
			else
			{
				m_LoadingStage = 2;
				CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Game initialization packets sent, proceeding to loading stage" );
				m_Game->SendAllChat( "Player [" + m_Name + "] has begun to stream this game." );
				m_GHost->DenyIP( GetExternalIPString( ), 60000, "streaming started" );
				m_BeganStreaming = true;
			}
			
			m_LastStageTicks = GetTicks( );
		}
		
		else if( m_LoadingStage == 2 && m_MapOK && GetTicks( ) - m_LastStageTicks > 5000 )
		{
			Send( m_Protocol->SEND_W3GS_SLOTINFO( m_Game->m_Slots, m_Game->GetRandomSeed( ), m_Game->GetStreamMapLayoutStyle( ), m_Game->GetStreamMapNumPlayers( ) ) );
			Send( m_Protocol->SEND_W3GS_COUNTDOWN_START( ) );
			Send( m_Protocol->SEND_W3GS_COUNTDOWN_END( ) );
			
			// say that everyone else loaded already
			for( vector<FakePlayer> :: iterator i = m_Game->m_FakePlayers.begin( ); i != m_Game->m_FakePlayers.end( ); ++i )
			{
				if( i->pid != m_Game->GetStreamPID( ) )
					Send( m_Protocol->SEND_W3GS_GAMELOADED_OTHERS( i->pid ) );
			}
			
			for( vector<CGamePlayer *> :: iterator i = m_Game->m_Players.begin( ); i != m_Game->m_Players.end( ); ++i )
			{
				if( (*i)->GetPID( ) != m_Game->GetStreamPID( ) )
					Send( m_Protocol->SEND_W3GS_GAMELOADED_OTHERS( (*i)->GetPID( ) ) );
			}
			
			m_LoadingStage = 3;
			m_LastStageTicks = GetTicks( );
			CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Game loading packets sent, waiting for user to load" );
		}
		
		else if( m_LoadingStage == 3 && m_FinishedLoading )
		{
			m_LoadingStage = 4;
			m_FakeStartTicks = GetTicks( );
			m_LastStageTicks = GetTicks( );
			CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Proceeding to streaming stage" );
		}
		
		else if( m_LoadingStage == 4 )
		{
			// send any game packets in our stream
			
			m_StreamPosition = GetTicks( ) - m_FakeStartTicks;
			
			for( ; m_ActionsIndex < m_Game->m_StreamPackets->size( ) && m_Game->m_StreamPackets->at( m_ActionsIndex )->m_Ticks < m_StreamPosition; m_ActionsIndex++ )
				Send( m_Game->m_StreamPackets->at( m_ActionsIndex )->m_Packet );
			
			// quit if we have finished watching the stream
			if( m_ActionsIndex >= m_Game->m_StreamPackets->size( ) )
				m_DeleteMe = true;
		}
		
		else if( m_LoadingStage == 9 && GetTicks( ) - m_LastStageTicks > 10000 )
			m_DeleteMe = true;
	}

	m_Socket->DoRecv( (fd_set *)fd );
	ExtractPackets( );
	ProcessPackets( );
	
	if( m_ConnectCheck && m_ConnectCheck->GetReady( ) )
	{
		bool Check = m_ConnectCheck->GetResult( );

		if( Check )
		{
			m_Checked = true;
			CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Authentication successful" );
		}
		else
		{
			m_DeleteMe = true;
        	m_GHost->DenyIP( GetExternalIPString( ), 60000, "failed connect check" );
		}

		m_GHost->m_DB->RecoverCallable( m_ConnectCheck );
		delete m_ConnectCheck;
		m_ConnectCheck= NULL;
	}
	
	// make sure we don't keep this socket open forever (disconnect after five seconds)
	if( !m_Game && GetTicks( ) - m_ConnectionTicks > 5000 )
	{
		m_DeleteMe = true;
        m_GHost->DenyIP( GetExternalIPString( ), 60000, "game not chosen within five seconds" );
	}
	
	// socket timeouts
	if( m_Socket && GetTime( ) - m_Socket->GetLastRecv( ) >= 30 )
	{
		m_DeleteMe = true;
		m_GHost->DenyIP( GetExternalIPString( ), 60000, "socket timeout" );
	}
	
	// ping
	if( GetTime( ) - m_LastPingTime >= 5 )
	{
		Send( m_Protocol->SEND_W3GS_PING_FROM_HOST( ) );
		m_LastPingTime = GetTime( );
	}

	return m_Socket->HasError( ) || !m_Socket->GetConnected( ) || m_DeleteMe;
}

void CStreamPlayer :: ExtractPackets( )
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
    			m_GHost->DenyIP( GetExternalIPString( ), 60000, "invalid packet (bad length)" );
				return;
			}
		}
		else
		{
			m_DeleteMe = true;
			m_GHost->DenyIP( GetExternalIPString( ), 60000, "invalid packet (bad header constant)" );
			return;
		}
	}
}

void CStreamPlayer :: ProcessPackets( )
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
			// the only packet we care about as a Stream player is W3GS_REQJOIN, ignore everything else

			switch( Packet->GetID( ) )
			{
			case CGameProtocol :: W3GS_REQJOIN:
				IncomingPlayer = m_Protocol->RECEIVE_W3GS_REQJOIN( Packet->GetData( ) );

				if( IncomingPlayer )
				{
					m_ConnectCheck = m_GHost->m_DB->ThreadedConnectCheck( IncomingPlayer->GetName( ), IncomingPlayer->GetEntryKey( ) );
					m_Name = IncomingPlayer->GetName( );
					
					CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "] Attempting to authenticate as [" + m_Name + "]" );
				}

				delete IncomingPlayer;
				IncomingPlayer = NULL;
				break;
			
			case CGameProtocol :: W3GS_CHAT_TO_HOST:
				ChatPlayer = m_Protocol->RECEIVE_W3GS_CHAT_TO_HOST( Packet->GetData( ) );
				
				if( ChatPlayer )
				{
					if( ChatPlayer->GetType( ) == CIncomingChatPlayer :: CTH_MESSAGE && !m_Game )
					{
						string TargetGame = ChatPlayer->GetMessage( );
						boost::mutex::scoped_lock lock( m_GHost->m_GamesMutex );
						
						for( vector<CBaseGame *> :: iterator i = m_GHost->m_Games.begin( ); i != m_GHost->m_Games.end( ); ++i )
						{
							if( (*i)->GetGameName( ) == TargetGame )
							{
								m_Game = *i;
								break;
							}
						}
						
						lock.unlock( );
						
						if( !m_Game )
						{
							CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Requested game [" + TargetGame + "] but no match found, disconnecting" );
							m_DeleteMe = true;
						}
						else
							CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Initializing with game [" + TargetGame + "]" );
					}
				}

				delete ChatPlayer;
				ChatPlayer = NULL;
				break;
			
			case CGameProtocol :: W3GS_MAPSIZE:
				MapSize = m_Protocol->RECEIVE_W3GS_MAPSIZE( Packet->GetData( ), m_GHost->m_Map->GetMapSize( ) );

				if( MapSize )
				{
					// player must have map already, otherwise kick
					if( MapSize->GetSizeFlag( ) == 1 && MapSize->GetMapSize( ) == m_Game->GetCachedMapSize( ) )
					{
						m_MapOK = true;
						SendChat( "Looks like everything is good, streaming will begin shortly." );
						CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] User has confirmed map OK." );
						
						// update player slots to reflect new download status
						// now we just use game slots since it should be identical
						Send( m_Protocol->SEND_W3GS_SLOTINFO( m_Game->m_Slots, m_Game->GetRandomSeed( ), m_Game->GetStreamMapLayoutStyle( ), m_Game->GetStreamMapNumPlayers( ) ) );
					}
					else
					{
						m_LoadingStage = 9;
						m_LastStageTicks = GetTicks( );
						SendChat( "Error: please download the map before streaming!" );
						CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Stream failure: user does not have the map!" );
					}
				}

				delete MapSize;
				MapSize = NULL;
				break;
			
			case CGameProtocol :: W3GS_GAMELOADED_SELF:
				if( m_Protocol->RECEIVE_W3GS_GAMELOADED_SELF( Packet->GetData( ) ) )
				{
					m_FinishedLoading = true;
					CONSOLE_Print( "[STREAM " + GetExternalIPString( ) + "/" + m_Name + "] Finished loading" );
					Send( m_Protocol->SEND_W3GS_GAMELOADED_OTHERS( m_Game->GetStreamPID( ) ) );
				}

				break;
			
			case CGameProtocol :: W3GS_LEAVEGAME:
				m_DeleteMe = true;
				break;
			}
		}

		delete Packet;
	}
}

void CStreamPlayer :: Send( BYTEARRAY data )
{
	if( m_Socket )
		m_Socket->PutBytes( data );
}

void CStreamPlayer :: SendChat( string message )
{
	// check m_LoadingStage > 0 to ensure we're executing from the game's thread

	if( m_LoadingStage > 0 && m_Game )
	{
		if( m_LoadingStage <= 2 )
		{
			if( message.size( ) > 254 )
				message = message.substr( 0, 254 );

			Send( m_Protocol->SEND_W3GS_CHAT_FROM_HOST( m_Game->GetStreamPID( ), UTIL_CreateByteArray( m_Game->GetStreamPID( ) ), 16, BYTEARRAY( ), message ) );
		}
		else
		{
			unsigned char ExtraFlags[] = { 3, 0, 0, 0 };

			// based on my limited testing it seems that the extra flags' first byte contains 3 plus the recipient's colour to denote a private message

			unsigned char SID = m_Game->GetSIDFromPID( m_Game->GetStreamPID( ) );

			if( SID < m_Game->m_Slots.size( ) )
				ExtraFlags[0] = 3 + m_Game->m_Slots[SID].GetColour( );

			if( message.size( ) > 127 )
				message = message.substr( 0, 127 );

			Send( m_Protocol->SEND_W3GS_CHAT_FROM_HOST( m_Game->GetStreamPID( ), UTIL_CreateByteArray( m_Game->GetStreamPID( ) ), 32, UTIL_CreateByteArray( ExtraFlags, 4 ), message ) );
		}
	}
}
