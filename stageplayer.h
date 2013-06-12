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

#ifndef STAGEPLAYER_H
#define STAGEPLAYER_H

class CTCPSocket;
class CCommandPacket;
class CGameProtocol;
class CGame;
class CCallableConnectCheck;
class CCallableScoreCheck;

//
// CStagePlayer
//

class CStagePlayer
{
public:
	CGameProtocol *m_Protocol;
	CGHost *m_GHost;

protected:
	CTCPSocket *m_Socket;
	uint32_t m_LoadingStage;					// 0: not in game loop; 1: nothing sent yet; 2: sent initialization; 3: waiting for mode selection; 4: ready; 9: terminating
	uint32_t m_JoinTicks;						// ticks when REQJOIN processed
	bool m_SpoofSent;							// whether or not spoof check has been sent
	
	uint32_t m_LastPingTime;					// last GetTime we pinged this streamer
	uint32_t m_LastScoreRangeIncrease;			// last GetTime when we increased our score range
	uint32_t m_LastStageTicks;					// ticks when m_LoadingStage last updated
	
	string m_Name;								// username
	string m_Realm;								// spoofed realm
	uint32_t m_PID;								// PID randomly assigned to this player
	uint32_t m_VirtualPID;						// PID for virtual player (so that we receive chat messages)
	uint32_t m_ChatPID;							// PID for broadcast messages
	double m_Score;
	double m_ScoreRange;						// allowed deviation from our score of players that we're willing to play with
	
	vector<uint32_t> m_MuteMessages;			// times player sent messages to determine if we should automute
	uint32_t m_MuteTicks;						// ticks when this player was muted, or 0 if not muted
	
	string m_CachedIP;
	CCallableConnectCheck *m_ConnectCheck;				// used to validate the stream player
	CCallableScoreCheck *m_ScoreCheck;			// used to determine player score
	bool m_Checked;
	bool m_MapOK;								// player confirmed has the map
	bool m_DeleteMe;
	uint32_t m_ConnectionTicks;					// ticks when initialized
	
	queue<CCommandPacket *> m_Packets;

public:
	CStagePlayer( CGameProtocol *nProtocol, CTCPSocket *nSocket, CGHost *nGHost );
	~CStagePlayer( );

	CTCPSocket *GetSocket( )				{ return m_Socket; }
	BYTEARRAY GetExternalIP( );
	string GetExternalIPString( );
	bool GetDeleteMe( )						{ return m_DeleteMe; }
	string GetName( )						{ return m_Name; }
	string GetRealm( )						{ return m_Realm; }
	uint32_t GetPID( )						{ return m_PID; }
	bool GetReady( )						{ return m_LoadingStage == 4; }
	double GetScore( )						{ return m_Score; }

	void SetSocket( CTCPSocket *nSocket )	{ m_Socket = nSocket; }
	void SetChecked( )						{ m_Checked = true; SendChat( "[System] Spoof check successful!" ); }

	// processing functions

	bool Update( void *fd );
	void ExtractPackets( );
	void ProcessPackets( );

	// other functions

	void Send( BYTEARRAY data );
	void SendChat( string message, bool local = true );
	bool IsScoreAcceptable( double nScore, int nScoreType );
	void RemoveVirtual( );
};


#endif
