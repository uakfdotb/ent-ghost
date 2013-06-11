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

#ifndef STREAMPLAYER_H
#define STREAMPLAYER_H

class CTCPSocket;
class CCommandPacket;
class CGameProtocol;
class CGame;
class CCallableConnectCheck;

//
// CStreamPlayer
//

class CStreamPlayer
{
public:
	CGameProtocol *m_Protocol;
	CGHost *m_GHost;
	CBaseGame *m_Game;

protected:
	CTCPSocket *m_Socket;
	uint32_t m_LoadingStage;					// 0: not in game loop; 1: nothing sent yet; 2: sent initialization; 3: sent countdown (wait for load); 4: streaming; 9: terminating
	uint32_t m_LastStageTicks;					// ticks when m_LoadingStage last updated
	uint32_t m_FakeStartTicks;					// ticks when we fake game start
	uint32_t m_StreamPosition;					// milliseconds elapsed since game start in our stream
	uint32_t m_ActionsIndex;					// index in the game's cached action packets
	
	bool m_BeganStreaming;						// whether we ever began streaming
	uint32_t m_LastPingTime;					// last GetTime we pinged this streamer
	string m_Name;								// forum username
	
	string m_CachedIP;
	CCallableConnectCheck *m_ConnectCheck;		// used to validate the stream player
	bool m_Checked;
	bool m_MapOK;								// player confirmed has the map
	bool m_DeleteMe;
	uint32_t m_ConnectionTicks;					// ticks when initialized
	bool m_FinishedLoading;						// whether player finished loading the game
	
	queue<CCommandPacket *> m_Packets;

public:
	CStreamPlayer( CGameProtocol *nProtocol, CTCPSocket *nSocket, CGHost *nGHost );
	~CStreamPlayer( );

	CTCPSocket *GetSocket( )				{ return m_Socket; }
	BYTEARRAY GetExternalIP( );
	string GetExternalIPString( );
	bool GetDeleteMe( )						{ return m_DeleteMe; }
	bool GetAssociated( )					{ return m_Game != NULL; }
	string GetName( )						{ return m_Name; }
	bool GetBeganStreaming( )				{ return m_BeganStreaming; }

	void SetSocket( CTCPSocket *nSocket )	{ m_Socket = nSocket; }
	void SetGame( CBaseGame *nGame )		{ m_Game = nGame; }

	// processing functions

	bool Update( void *fd );
	void ExtractPackets( );
	void ProcessPackets( );

	// other functions

	void Send( BYTEARRAY data );
	void SendChat( string message );
};


#endif
