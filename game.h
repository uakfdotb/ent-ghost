/*

   Copyright [2008] [Trevor Hogan]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#ifndef GAME_H
#define GAME_H

//
// CGame
//

class CDBBan;
class CDBGame;
class CDBGamePlayer;
class CStats;
class CCallableBanCheck;
class CCallableBanAdd;
class CCallableGameAdd;
class CCallableGamePlayerSummaryCheck;
class CCallableDotAPlayerSummaryCheck;
class CCallableVampPlayerSummaryCheck;
class CCallableTreePlayerSummaryCheck;
class CCallableSnipePlayerSummaryCheck;
class CCallableShipsPlayerSummaryCheck;
class CCallableW3MMDPlayerSummaryCheck;

typedef pair<string,CCallableBanCheck *> PairedBanCheck;
typedef pair<string,CCallableBanAdd *> PairedBanAdd;
typedef pair<string,CCallableGamePlayerSummaryCheck *> PairedGPSCheck;
typedef pair<string,CCallableDotAPlayerSummaryCheck *> PairedDPSCheck;
typedef pair<string,CCallableVampPlayerSummaryCheck *> PairedVPSCheck;
typedef pair<string,CCallableTreePlayerSummaryCheck *> PairedTPSCheck;
typedef pair<string,CCallableSnipePlayerSummaryCheck *> PairedSPSCheck;
typedef pair<string,CCallableShipsPlayerSummaryCheck *> PairedBPSCheck;
typedef pair<string,CCallableW3MMDPlayerSummaryCheck *> PairedWPSCheck;

class CGame : public CBaseGame
{
private:
    uint32_t m_Guess;
protected:
	CDBBan *m_DBBanLast;						// last ban for the !banlast command - this is a pointer to one of the items in m_DBBans
	vector<CDBBan *> m_DBBans;					// vector of potential ban data for the database (see the Update function for more info, it's not as straightforward as you might think)
	CDBGame *m_DBGame;							// potential game data for the database
	vector<CDBGamePlayer *> m_DBGamePlayers;	// vector of potential gameplayer data for the database
	CStats *m_Stats;							// class to keep track of game stats such as kills/deaths/assists in dota
	CCallableGameAdd *m_CallableGameAdd;		// threaded database game addition in progress
	vector<PairedBanCheck> m_PairedBanChecks;	// vector of paired threaded database ban checks in progress
	vector<PairedBanAdd> m_PairedBanAdds;		// vector of paired threaded database ban adds in progress
	vector<PairedGPSCheck> m_PairedGPSChecks;	// vector of paired threaded database game player summary checks in progress
	vector<PairedDPSCheck> m_PairedDPSChecks;	// vector of paired threaded database DotA player summary checks in progress
	vector<PairedVPSCheck> m_PairedVPSChecks;	// vector of paired threaded database vamp player summary checks in progress
	vector<PairedTPSCheck> m_PairedTPSChecks;	// vector of paired threaded database treetag player summary checks in progress
	vector<PairedSPSCheck> m_PairedSPSChecks;	// vector of paired threaded database sniper player summary checks in progress
	vector<PairedBPSCheck> m_PairedBPSChecks;	// vector of paired threaded database battleships player summary checks in progress
	vector<PairedWPSCheck> m_PairedWPSChecks;	// vector of paired threaded database battleships player summary checks in progress
	
    string m_MapType;							// recorded map type after game starts because map is deleted
	vector<string> m_AutoBans;
	
	bool IsAutoBanned( string name );

public:
	CGame( CGHost *nGHost, CMap *nMap, CSaveGame *nSaveGame, uint16_t nHostPort, unsigned char nGameState, string nGameName, string nOwnerName, string nCreatorName, string nCreatorServer );
	virtual ~CGame( );

	virtual bool Update( void *fd, void *send_fd );
	virtual CGamePlayer *EventPlayerJoined( CPotentialPlayer *potential, CIncomingJoinPlayer *joinPlayer, double *score );
	virtual void EventPlayerDeleted( CGamePlayer *player );
	virtual bool EventPlayerAction( CGamePlayer *player, CIncomingAction *action );
	virtual bool EventPlayerBotCommand( CGamePlayer *player, string command, string payload );
	virtual void EventGameStarted( );
	virtual bool IsGameDataSaved( );
	virtual void SaveGameData( );
    virtual uint32_t GetGuess() {return m_Guess;}
    virtual void SetGuess( uint32_t nGuess )     { m_Guess = nGuess; }

};

#endif
