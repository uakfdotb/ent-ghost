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

#ifndef GAME_ADMIN_H
#define GAME_ADMIN_H

//
// CAdminGame
//

class CCallableAdminCount;
class CCallableAdminAdd;
class CCallableAdminRemove;
class CCallableBanCount;
// class CCallableBanAdd;
class CCallableBanRemove;

typedef pair<string,CCallableAdminCount *> PairedAdminCount;
typedef pair<string,CCallableAdminAdd *> PairedAdminAdd;
typedef pair<string,CCallableAdminRemove *> PairedAdminRemove;
typedef pair<string,CCallableBanCount *> PairedBanCount;
// typedef pair<string,CCallableBanAdd *> PairedBanAdd;
typedef pair<string,CCallableBanRemove *> PairedBanRemove;

typedef pair<string,uint32_t> TempBan;

class CAdminGame : public CBaseGame
{
protected:
	string m_Password;
	vector<TempBan> m_TempBans;
	vector<PairedAdminCount> m_PairedAdminCounts;	// vector of paired threaded database admin counts in progress
	vector<PairedAdminAdd> m_PairedAdminAdds;		// vector of paired threaded database admin adds in progress
	vector<PairedAdminRemove> m_PairedAdminRemoves;	// vector of paired threaded database admin removes in progress
	vector<PairedBanCount> m_PairedBanCounts;		// vector of paired threaded database ban counts in progress
	// vector<PairedBanAdd> m_PairedBanAdds;		// vector of paired threaded database ban adds in progress
	vector<PairedBanRemove> m_PairedBanRemoves;		// vector of paired threaded database ban removes in progress

public:
	CAdminGame( CGHost *nGHost, CMap *nMap, CSaveGame *nSaveGame, uint16_t nHostPort, unsigned char nGameState, string nGameName, string nPassword );
	virtual ~CAdminGame( );

	virtual bool Update( void *fd, void *send_fd );
	virtual void SendAdminChat( string message );
	virtual void SendWelcomeMessage( CGamePlayer *player );
	virtual CGamePlayer *EventPlayerJoined( CPotentialPlayer *potential, CIncomingJoinPlayer *joinPlayer, double *score );
	virtual bool EventPlayerBotCommand( CGamePlayer *player, string command, string payload );
};

#endif
