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
#include "crc32.h"
#include "sha1.h"
#include "csvparser.h"
#include "config.h"
#include "language.h"
#include "socket.h"
#include "ghostdb.h"
#include "ghostdbmysql.h"
#include "bnet.h"
#include "map.h"
#include "packed.h"
#include "savegame.h"
#include "gameplayer.h"
#include "gameprotocol.h"
#include "gcbiprotocol.h"
#include "amhprotocol.h"
#include "gpsprotocol.h"
#include "game_base.h"
#include "game.h"
#include "streamplayer.h"
#include "stageplayer.h"

#include <signal.h>
#include <execinfo.h> //to generate stack trace-like thing on exception
#include <stdlib.h>

#ifdef WIN32
 #include <ws2tcpip.h>		// for WSAIoctl
#endif

#define __STORMLIB_SELF__
#include <stormlib/StormLib.h>

#ifdef WIN32
 #include <windows.h>
 #include <winsock.h>
#endif

#include <time.h>

#ifndef WIN32
 #include <sys/time.h>
#endif

#ifdef __APPLE__
 #include <mach/mach_time.h>
#endif

string gCFGFile;
string gLogFile;
uint32_t gLogMethod;
ofstream *gLog = NULL;
CGHost *gGHost = NULL;
vector<string> gLogQueue;
uint32_t gLogLastTicks;
boost::mutex PrintMutex;

uint32_t GetTime( )
{
	return GetTicks( ) / 1000;
}

uint32_t GetTicks( )
{
#ifdef WIN32
	// don't use GetTickCount anymore because it's not accurate enough (~16ms resolution)
	// don't use QueryPerformanceCounter anymore because it isn't guaranteed to be strictly increasing on some systems and thus requires "smoothing" code
	// use timeGetTime instead, which typically has a high resolution (5ms or more) but we request a lower resolution on startup

	return timeGetTime( );
#elif __APPLE__
	uint64_t current = mach_absolute_time( );
	static mach_timebase_info_data_t info = { 0, 0 };
	// get timebase info
	if( info.denom == 0 )
		mach_timebase_info( &info );
	uint64_t elapsednano = current * ( info.numer / info.denom );
	// convert ns to ms
	return elapsednano / 1e6;
#else
	uint32_t ticks;
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	ticks = t.tv_sec * 1000;
	ticks += t.tv_nsec / 1000000;
	return ticks;
#endif
}

void SignalCatcher2( int s )
{
	CONSOLE_Print( "[!!!] caught signal " + UTIL_ToString( s ) + ", exiting NOW" );

	if( gGHost )
	{
		if( gGHost->m_Exiting )
			exit( 1 );
		else
			gGHost->m_Exiting = true;
	}
	else
		exit( 1 );
}

void SignalCatcher( int s )
{
	// signal( SIGABRT, SignalCatcher2 );
	signal( SIGINT, SignalCatcher2 );

	CONSOLE_Print( "[!!!] caught signal " + UTIL_ToString( s ) + ", exiting nicely" );

	if( gGHost )
		gGHost->m_ExitingNice = true;
	else
		exit( 1 );
}

void handler()
{
    void *trace_elems[20];
    int trace_elem_count(backtrace( trace_elems, 20 ));
    char **stack_syms(backtrace_symbols( trace_elems, trace_elem_count ));
    for ( int i = 0 ; i < trace_elem_count ; ++i )
    {
        std::cout << stack_syms[i] << "\n";
    }
    free( stack_syms );

    exit(1);
}

void CONSOLE_Print( string message )
{
	boost::mutex::scoped_lock printLock( PrintMutex );
	gLogQueue.push_back( message );
	printLock.unlock( );
}

void CONSOLE_Flush( )
{
	vector<string> logQueue;
	boost::mutex::scoped_lock printLock( PrintMutex );
	gLogQueue.swap( logQueue );
	printLock.unlock( );

	if( logQueue.empty( ) )
		return;

	for( vector<string>::iterator it = logQueue.begin( ); it != logQueue.end( ); it++) {
		cout << *it << endl;
	}

	// logging

	if( !gLogFile.empty( ) )
	{
		time_t Now = time( NULL );
		string Time = asctime( localtime( &Now ) );

		// erase the newline

		Time.erase( Time.size( ) - 1 );

		if( gLogMethod == 1 )
		{
			ofstream Log;
			Log.open( gLogFile.c_str( ), ios :: app );

			if( !Log.fail( ) )
			{
				for( vector<string>::iterator it = logQueue.begin( ); it != logQueue.end( ); it++) {
					Log << "[" << Time << "] " << *it << endl;
				}
				
				Log.close( );
			}
		}
		else if( gLogMethod == 2 )
		{
			if( gLog && !gLog->fail( ) )
			{
				for( vector<string>::iterator it = logQueue.begin( ); it != logQueue.end( ); it++) {
					*gLog << "[" << Time << "] " << *it << endl;
				}
				
				gLog->flush( );
			}
		}
	}
}

void DEBUG_Print( string message )
{
	cout << message << endl;
}

void DEBUG_Print( BYTEARRAY b )
{
	cout << "{ ";

        for( unsigned int i = 0; i < b.size( ); ++i )
		cout << hex << (int)b[i] << " ";

	cout << "}" << endl;
}

//
// main
//

int main( int argc, char **argv )
{
	srand( time( NULL ) );

	gCFGFile = "ghost.cfg";

	if( argc > 1 && argv[1] )
		gCFGFile = argv[1];

	// read config file

	CConfig CFG;
	CFG.Read( "default.cfg" );
	CFG.Read( gCFGFile );
	gLogFile = CFG.GetString( "bot_log", string( ) );
	gLogMethod = CFG.GetInt( "bot_logmethod", 1 );

	if( !gLogFile.empty( ) )
	{
		if( gLogMethod == 1 )
		{
			// log method 1: open, append, and close the log for every message
			// this works well on Linux but poorly on Windows, particularly as the log file grows in size
			// the log file can be edited/moved/deleted while GHost++ is running
		}
		else if( gLogMethod == 2 )
		{
			// log method 2: open the log on startup, flush the log for every message, close the log on shutdown
			// the log file CANNOT be edited/moved/deleted while GHost++ is running

			gLog = new ofstream( );
			gLog->open( gLogFile.c_str( ), ios :: app );
		}
	}

	CONSOLE_Print( "[GHOST] starting up" );

	if( !gLogFile.empty( ) )
	{
		if( gLogMethod == 1 )
			CONSOLE_Print( "[GHOST] using log method 1, logging is enabled and [" + gLogFile + "] will not be locked" );
		else if( gLogMethod == 2 )
		{
			if( gLog->fail( ) )
				CONSOLE_Print( "[GHOST] using log method 2 but unable to open [" + gLogFile + "] for appending, logging is disabled" );
			else
				CONSOLE_Print( "[GHOST] using log method 2, logging is enabled and [" + gLogFile + "] is now locked" );
		}
	}
	else
		CONSOLE_Print( "[GHOST] no log file specified, logging is disabled" );

	// catch SIGABRT and SIGINT

	// signal( SIGABRT, SignalCatcher );
	signal( SIGINT, SignalCatcher );
	std::set_terminate( handler ); //to generate stack trace on fail

#ifndef WIN32
	// disable SIGPIPE since some systems like OS X don't define MSG_NOSIGNAL

	signal( SIGPIPE, SIG_IGN );
#endif

#ifdef WIN32
	// initialize timer resolution
	// attempt to set the resolution as low as possible from 1ms to 5ms

	unsigned int TimerResolution = 0;

        for( unsigned int i = 1; i <= 5; ++i )
	{
		if( timeBeginPeriod( i ) == TIMERR_NOERROR )
		{
			TimerResolution = i;
			break;
		}
		else if( i < 5 )
			CONSOLE_Print( "[GHOST] error setting Windows timer resolution to " + UTIL_ToString( i ) + " milliseconds, trying a higher resolution" );
		else
		{
			CONSOLE_Print( "[GHOST] error setting Windows timer resolution" );
			return 1;
		}
	}

	CONSOLE_Print( "[GHOST] using Windows timer with resolution " + UTIL_ToString( TimerResolution ) + " milliseconds" );
#elif __APPLE__
	// not sure how to get the resolution
#else
	// print the timer resolution

	struct timespec Resolution;

	if( clock_getres( CLOCK_MONOTONIC, &Resolution ) == -1 )
		CONSOLE_Print( "[GHOST] error getting monotonic timer resolution" );
	else
		CONSOLE_Print( "[GHOST] using monotonic timer with resolution " + UTIL_ToString( (double)( Resolution.tv_nsec / 1000 ), 2 ) + " microseconds" );
#endif

#ifdef WIN32
	// initialize winsock

	CONSOLE_Print( "[GHOST] starting winsock" );
	WSADATA wsadata;

	if( WSAStartup( MAKEWORD( 2, 2 ), &wsadata ) != 0 )
	{
		CONSOLE_Print( "[GHOST] error starting winsock" );
		return 1;
	}

	// increase process priority

	CONSOLE_Print( "[GHOST] setting process priority to \"above normal\"" );
	SetPriorityClass( GetCurrentProcess( ), ABOVE_NORMAL_PRIORITY_CLASS );
#endif

	// initialize ghost

	gGHost = new CGHost( &CFG );

	while( 1 )
	{
		// block for 50ms on all sockets - if you intend to perform any timed actions more frequently you should change this
		// that said it's likely we'll loop more often than this due to there being data waiting on one of the sockets but there aren't any guarantees

		if( gGHost->Update( 50000 ) )
			break;
	}

	// shutdown ghost

	CONSOLE_Print( "[GHOST] shutting down" );
	delete gGHost;
	gGHost = NULL;

#ifdef WIN32
	// shutdown winsock

	CONSOLE_Print( "[GHOST] shutting down winsock" );
	WSACleanup( );

	// shutdown timer

	timeEndPeriod( TimerResolution );
#endif

	if( gLog )
	{
		if( !gLog->fail( ) )
			gLog->close( );

		delete gLog;
	}

	return 0;
}

//
// CGHost
//

CGHost :: CGHost( CConfig *CFG )
{
	for( int i = 0; i < 10; i++)
	{
		string Key = "udp_broadcasttarget";

		if( i != 0 )
			Key += UTIL_ToString( i );

		string Target = CFG->GetString( Key, string( ) );

		if( Target.empty( ) )
			continue;
		
		CUDPSocket *UDPSocket = new CUDPSocket( );
		UDPSocket->SetBroadcastTarget( Target );
		UDPSocket->SetDontRoute( CFG->GetInt( "udp_dontroute", 0 ) == 0 ? false : true );
		m_UDPSockets.push_back( UDPSocket );
	}
	m_LocalSocket = new CUDPSocket( );
	m_LocalSocket->SetBroadcastTarget( "localhost" );
	m_LocalSocket->SetDontRoute( CFG->GetInt( "udp_dontroute", 0 ) == 0 ? false : true );
	m_ReconnectSocket = NULL;
	m_StreamSocket = NULL;
	m_GPSProtocol = new CGPSProtocol( );
	m_GCBIProtocol = new CGCBIProtocol( );
	m_AMHProtocol = new CAMHProtocol( );
	m_GameProtocol = new CGameProtocol( this );
	m_CRC = new CCRC32( );
	m_CRC->Initialize( );
	m_SHA = new CSHA1( );
	m_CurrentGame = NULL;
	m_LastDenyCleanTime = 0;

	m_CallableSpoofList = NULL;
	m_LastSpoofRefreshTime = 0;
	
	CONSOLE_Print( "[GHOST] opening primary database" );

	m_DB = new CGHostDBMySQL( CFG );

	// get a list of local IP addresses
	// this list is used elsewhere to determine if a player connecting to the bot is local or not

	CONSOLE_Print( "[GHOST] attempting to find local IP addresses" );

#ifdef WIN32
	// use a more reliable Windows specific method since the portable method doesn't always work properly on Windows
	// code stolen from: http://tangentsoft.net/wskfaq/examples/getifaces.html

	SOCKET sd = WSASocket( AF_INET, SOCK_DGRAM, 0, 0, 0, 0 );

	if( sd == SOCKET_ERROR )
		CONSOLE_Print( "[GHOST] error finding local IP addresses - failed to create socket (error code " + UTIL_ToString( WSAGetLastError( ) ) + ")" );
	else
	{
		INTERFACE_INFO InterfaceList[20];
		unsigned long nBytesReturned;

		if( WSAIoctl( sd, SIO_GET_INTERFACE_LIST, 0, 0, &InterfaceList, sizeof(InterfaceList), &nBytesReturned, 0, 0 ) == SOCKET_ERROR )
			CONSOLE_Print( "[GHOST] error finding local IP addresses - WSAIoctl failed (error code " + UTIL_ToString( WSAGetLastError( ) ) + ")" );
		else
		{
			int nNumInterfaces = nBytesReturned / sizeof(INTERFACE_INFO);

                        for( int i = 0; i < nNumInterfaces; ++i )
			{
				sockaddr_in *pAddress;
				pAddress = (sockaddr_in *)&(InterfaceList[i].iiAddress);
				CONSOLE_Print( "[GHOST] local IP address #" + UTIL_ToString( i + 1 ) + " is [" + string( inet_ntoa( pAddress->sin_addr ) ) + "]" );
				m_LocalAddresses.push_back( UTIL_CreateByteArray( (uint32_t)pAddress->sin_addr.s_addr, false ) );
			}
		}

		closesocket( sd );
	}
#else
	// use a portable method

	char HostName[255];

	if( gethostname( HostName, 255 ) == SOCKET_ERROR )
		CONSOLE_Print( "[GHOST] error finding local IP addresses - failed to get local hostname" );
	else
	{
		CONSOLE_Print( "[GHOST] local hostname is [" + string( HostName ) + "]" );
		struct hostent *HostEnt = gethostbyname( HostName );

		if( !HostEnt )
			CONSOLE_Print( "[GHOST] error finding local IP addresses - gethostbyname failed" );
		else
		{
                        for( int i = 0; HostEnt->h_addr_list[i] != NULL; ++i )
			{
				struct in_addr Address;
				memcpy( &Address, HostEnt->h_addr_list[i], sizeof(struct in_addr) );
				CONSOLE_Print( "[GHOST] local IP address #" + UTIL_ToString( i + 1 ) + " is [" + string( inet_ntoa( Address ) ) + "]" );
				m_LocalAddresses.push_back( UTIL_CreateByteArray( (uint32_t)Address.s_addr, false ) );
			}
		}
	}
#endif

	m_Language = NULL;
	m_Exiting = false;
	m_ExitingNice = false;
	m_Enabled = true;
	m_Version = "17.1";
	m_HostCounter = 1;
	m_AutoHostMaximumGames = CFG->GetInt( "autohost_maxgames", 0 );
	m_AutoHostAutoStartPlayers = CFG->GetInt( "autohost_startplayers", 0 );
	m_AutoHostGameName = CFG->GetString( "autohost_gamename", string( ) );
	m_AutoHostOwner = CFG->GetString( "autohost_owner", string( ) );
	m_LocalIPs = CFG->GetString( "bot_local", "127.0.0.1 127.0.1.1" );
	m_LastAutoHostTime = GetTime( );
	m_AutoHostMatchMaking = false;
	m_AutoHostMinimumScore = 0.0;
	m_AutoHostMaximumScore = 0.0;
	m_AllGamesFinished = false;
	m_AllGamesFinishedTime = 0;
	m_TFT = CFG->GetInt( "bot_tft", 1 ) == 0 ? false : true;

	if( m_TFT )
		CONSOLE_Print( "[GHOST] acting as Warcraft III: The Frozen Throne" );
	else
		CONSOLE_Print( "[GHOST] acting as Warcraft III: Reign of Chaos" );

	m_HostPort = CFG->GetInt( "bot_hostport", 6112 );
	m_Reconnect = CFG->GetInt( "bot_reconnect", 1 ) == 0 ? false : true;
	m_ReconnectPort = CFG->GetInt( "bot_reconnectport", 6114 );
	m_DefaultMap = CFG->GetString( "bot_defaultmap", "map" );
	m_LANWar3Version = CFG->GetInt( "lan_war3version", 24 );
	m_ReplayWar3Version = CFG->GetInt( "replay_war3version", 24 );
	m_ReplayBuildNumber = CFG->GetInt( "replay_buildnumber", 6059 );
	SetConfigs( CFG );

	// load the battle.net connections
	// we're just loading the config data and creating the CBNET classes here, the connections are established later (in the Update function)

        for( uint32_t i = 1; i < 10; ++i )
	{
		string Prefix;

		if( i == 1 )
			Prefix = "bnet_";
		else
			Prefix = "bnet" + UTIL_ToString( i ) + "_";

		string Server = CFG->GetString( Prefix + "server", string( ) );
		string ServerAlias = CFG->GetString( Prefix + "serveralias", string( ) );
		string CDKeyROC = CFG->GetString( Prefix + "cdkeyroc", string( ) );
		string CDKeyTFT = CFG->GetString( Prefix + "cdkeytft", string( ) );
		string CountryAbbrev = CFG->GetString( Prefix + "countryabbrev", "USA" );
		string Country = CFG->GetString( Prefix + "country", "United States" );
		string Locale = CFG->GetString( Prefix + "locale", "system" );
		uint32_t LocaleID;

		if( Locale == "system" )
		{
#ifdef WIN32
			LocaleID = GetUserDefaultLangID( );
#else
			LocaleID = 1033;
#endif
		}
		else
			LocaleID = UTIL_ToUInt32( Locale );

		string UserName = CFG->GetString( Prefix + "username", string( ) );
		string UserPassword = CFG->GetString( Prefix + "password", string( ) );
		string KeyOwnerName = CFG->GetString( Prefix + "keyownername", "GHost" );
		string FirstChannel = CFG->GetString( Prefix + "firstchannel", "The Void" );
		string RootAdmin = CFG->GetString( Prefix + "rootadmin", string( ) );
		string BNETCommandTrigger = CFG->GetString( Prefix + "commandtrigger", "!" );

		if( BNETCommandTrigger.empty( ) )
			BNETCommandTrigger = "!";

		bool HoldFriends = CFG->GetInt( Prefix + "holdfriends", 1 ) == 0 ? false : true;
		bool HoldClan = CFG->GetInt( Prefix + "holdclan", 1 ) == 0 ? false : true;
		bool PublicCommands = CFG->GetInt( Prefix + "publiccommands", 1 ) == 0 ? false : true;
		string BNLSServer = CFG->GetString( Prefix + "bnlsserver", string( ) );
		int BNLSPort = CFG->GetInt( Prefix + "bnlsport", 9367 );
		int BNLSWardenCookie = CFG->GetInt( Prefix + "bnlswardencookie", 0 );
		unsigned char War3Version = CFG->GetInt( Prefix + "custom_war3version", 24 );
		BYTEARRAY EXEVersion = UTIL_ExtractNumbers( CFG->GetString( Prefix + "custom_exeversion", string( ) ), 4 );
		BYTEARRAY EXEVersionHash = UTIL_ExtractNumbers( CFG->GetString( Prefix + "custom_exeversionhash", string( ) ), 4 );
		string PasswordHashType = CFG->GetString( Prefix + "custom_passwordhashtype", string( ) );
		string PVPGNRealmName = CFG->GetString( Prefix + "custom_pvpgnrealmname", "PvPGN Realm" );
		uint32_t MaxMessageLength = CFG->GetInt( Prefix + "custom_maxmessagelength", 200 );

		if( Server.empty( ) )
			break;

		if( CDKeyROC.empty( ) )
		{
			CONSOLE_Print( "[GHOST] missing " + Prefix + "cdkeyroc, skipping this battle.net connection" );
			continue;
		}

		if( m_TFT && CDKeyTFT.empty( ) )
		{
			CONSOLE_Print( "[GHOST] missing " + Prefix + "cdkeytft, skipping this battle.net connection" );
			continue;
		}

		if( UserName.empty( ) )
		{
			CONSOLE_Print( "[GHOST] missing " + Prefix + "username, skipping this battle.net connection" );
			continue;
		}

		if( UserPassword.empty( ) )
		{
			CONSOLE_Print( "[GHOST] missing " + Prefix + "password, skipping this battle.net connection" );
			continue;
		}

		CONSOLE_Print( "[GHOST] found battle.net connection #" + UTIL_ToString( i ) + " for server [" + Server + "]" );

		if( Locale == "system" )
		{
#ifdef WIN32
			CONSOLE_Print( "[GHOST] using system locale of " + UTIL_ToString( LocaleID ) );
#else
			CONSOLE_Print( "[GHOST] unable to get system locale, using default locale of 1033" );
#endif
		}

		m_BNETs.push_back( new CBNET( this, Server, ServerAlias, BNLSServer, (uint16_t)BNLSPort, (uint32_t)BNLSWardenCookie, CDKeyROC, CDKeyTFT, CountryAbbrev, Country, LocaleID, UserName, UserPassword, KeyOwnerName, FirstChannel, RootAdmin, BNETCommandTrigger[0], HoldFriends, HoldClan, PublicCommands, War3Version, EXEVersion, EXEVersionHash, PasswordHashType, PVPGNRealmName, MaxMessageLength, i ) );
	}

	if( m_BNETs.empty( ) )
		CONSOLE_Print( "[GHOST] warning - no battle.net connections found in config file" );

	// extract common.j and blizzard.j from War3Patch.mpq if we can
	// these two files are necessary for calculating "map_crc" when loading maps so we make sure to do it before loading the default map
	// see CMap :: Load for more information

	ExtractScripts( );

	// load the default maps (note: make sure to run ExtractScripts first)

	if( m_DefaultMap.size( ) < 4 || m_DefaultMap.substr( m_DefaultMap.size( ) - 4 ) != ".cfg" )
	{
		m_DefaultMap += ".cfg";
		CONSOLE_Print( "[GHOST] adding \".cfg\" to default map -> new default is [" + m_DefaultMap + "]" );
	}

	CConfig MapCFG;
	MapCFG.Read( m_MapCFGPath + m_DefaultMap );
	m_Map = new CMap( this, &MapCFG, m_MapCFGPath + m_DefaultMap );
	m_MapGameCreateRequest = NULL;

	for( int i = 0; i < 100; i++)
	{
		string AMKey = "autohost_map" + UTIL_ToString( i );
		
		if( i == 0 )
			AMKey = "bot_defaultmap";
		
		string AutoHostMapCFGString = CFG->GetString( AMKey , string( ) );
		
		if( AutoHostMapCFGString.empty( ) )
		{
			continue;
		}
		
		if( AutoHostMapCFGString.size( ) < 4 || AutoHostMapCFGString.substr( AutoHostMapCFGString.size( ) - 4 ) != ".cfg" )
		{
			AutoHostMapCFGString += ".cfg";
			CONSOLE_Print( "[GHOST] adding \".cfg\" to autohost game map -> new name is [" + AutoHostMapCFGString + "]" );
		}
		
		CConfig AutoHostMapCFG;
		AutoHostMapCFG.Read( m_MapCFGPath + AutoHostMapCFGString );
		CMap *AutoHostMap = new CMap( this, &AutoHostMapCFG, m_MapCFGPath + AutoHostMapCFGString );
		m_AutoHostMap.push_back( new CMap( *AutoHostMap ) );
	}

	m_SaveGame = new CSaveGame( );

	if( m_BNETs.empty( ) )
		CONSOLE_Print( "[GHOST] warning - no battle.net connections found" );

	CONSOLE_Print( "[GHOST] GHost++ Version " + m_Version + " (with MySQL support)" );
    CONSOLE_Print("[GHOST] Loading slap phrases...");

	ifstream phrasein;
	phrasein.open( "phrase.txt" );

	if( phrasein.fail( ) )
	{
        CONSOLE_Print("[GHOST] Slap phrase load failed!");
        perror("Fail");
    }
	else
	{
		string Line;

		while( !phrasein.eof( ) )
		{
			getline( phrasein, Line );

			if( !Line.empty( ) )
                m_SlapPhrases.push_back(Line);
		}

		phrasein.close( );
	}
	
	m_FlameTriggers.push_back("cunt");
	m_FlameTriggers.push_back("bitch");
	m_FlameTriggers.push_back("whore");
	m_FlameTriggers.push_back("retard");
	m_FlameTriggers.push_back("nigger");
	m_FlameTriggers.push_back("dumb");
	m_FlameTriggers.push_back("fuck you");
	m_FlameTriggers.push_back("fuck u");
	m_FlameTriggers.push_back("you suck");
	m_FlameTriggers.push_back("u suck");
	m_FlameTriggers.push_back("fucking noob");
	m_FlameTriggers.push_back("fuck off");
	m_FlameTriggers.push_back("stupid");
	m_FlameTriggers.push_back("noob as fuck");
	m_FlameTriggers.push_back("idiot");
	m_FlameTriggers.push_back("moron");
	m_FlameTriggers.push_back("shithead");
	m_FlameTriggers.push_back("assfuck");
	m_FlameTriggers.push_back("asshole");
	m_FlameTriggers.push_back("is shit");
	m_FlameTriggers.push_back("are shit");
	m_FlameTriggers.push_back("shitty");
	m_FlameTriggers.push_back("pussy");
	m_FlameTriggers.push_back("loser");
	m_FlameTriggers.push_back("fucking bad");
	m_FlameTriggers.push_back("faggot");
	m_FlameTriggers.push_back("dick");
	m_FlameTriggers.push_back("raizen");
	
	// clear the gamelist for this bot, in case there's residual entries
	m_Callables.push_back( m_DB->ThreadedGameUpdate(0, "", "", "", "", 0, "", 0, 0, false, false) );
}

CGHost :: ~CGHost( )
{
	for( vector<CUDPSocket *> :: iterator i = m_UDPSockets.begin( ); i != m_UDPSockets.end( ); ++i )
		delete *i;
		
	delete m_LocalSocket;
	delete m_ReconnectSocket;
	delete m_StreamSocket;

	for( vector<CTCPSocket *> :: iterator i = m_ReconnectSockets.begin( ); i != m_ReconnectSockets.end( ); ++i )
		delete *i;

	for( vector<CStreamPlayer *> :: iterator i = m_StreamPlayers.begin( ); i != m_StreamPlayers.end( ); ++i )
		delete *i;

	delete m_GPSProtocol;
	delete m_GCBIProtocol;
	delete m_GameProtocol;
	delete m_AMHProtocol;
	delete m_CRC;
	delete m_SHA;

	for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		delete *i;

	if( m_CurrentGame )
		m_CurrentGame->doDelete();

	boost::mutex::scoped_lock lock( m_GamesMutex );
	for( vector<CBaseGame *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); ++i )
		(*i)->doDelete();
	lock.unlock( );

	delete m_DB;

	// warning: we don't delete any entries of m_Callables here because we can't be guaranteed that the associated threads have terminated
	// this is fine if the program is currently exiting because the OS will clean up after us
	// but if you try to recreate the CGHost object within a single session you will probably leak resources!

	if( !m_Callables.empty( ) )
		CONSOLE_Print( "[GHOST] warning - " + UTIL_ToString( m_Callables.size( ) ) + " orphaned callables were leaked (this is not an error)" );

	delete m_Language;
	delete m_Map;
	delete m_AdminMap;
	
	ClearAutoHostMap( );
	
	delete m_SaveGame;
}

bool CGHost :: Update( long usecBlock )
{
	// todotodo: do we really want to shutdown if there's a database error? is there any way to recover from this?

	if( m_DB->HasError( ) )
	{
		CONSOLE_Print( "[GHOST] database error - " + m_DB->GetError( ) );
		return true;
	}

	boost::mutex::scoped_lock gamesLock( m_GamesMutex );

	// get rid of any deleted games
	for( vector<CBaseGame *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); )
	{
		if( (*i)->readyDelete( ) )
		{
			delete *i;
			m_Games.erase( i );
		} else {
			++i;
		}
	}
	
	if( m_CurrentGame && m_CurrentGame->readyDelete( ) )
	{
        for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
        {
            (*i)->QueueGameUncreate( );
            (*i)->QueueEnterChat( );
        }
        
		delete m_CurrentGame;
		m_CurrentGame = NULL;
	}
	
	gamesLock.unlock( );

	// try to exit nicely if requested to do so

	if( m_ExitingNice )
	{
		if( !m_BNETs.empty( ) )
		{
			CONSOLE_Print( "[GHOST] deleting all battle.net connections in preparation for exiting nicely" );

                        for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
				delete *i;

			m_BNETs.clear( );
		}

		if( m_CurrentGame )
		{
			CONSOLE_Print( "[GHOST] deleting current game in preparation for exiting nicely" );
			m_CurrentGame->doDelete( );
			m_CurrentGame = NULL;
		}
		
		if( !m_StagePlayers.empty( ) )
		{
			for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); i++ )
				delete *i;
			
			m_StagePlayers.clear( );
		}

		if( m_Games.empty( ) )
		{
			if( !m_AllGamesFinished )
			{
				CONSOLE_Print( "[GHOST] all games finished, waiting 60 seconds for threads to finish" );
				CONSOLE_Print( "[GHOST] there are " + UTIL_ToString( m_Callables.size( ) ) + " threads in progress" );
				m_AllGamesFinished = true;
				m_AllGamesFinishedTime = GetTime( );
			}
			else
			{
				if( m_Callables.empty( ) )
				{
					CONSOLE_Print( "[GHOST] all threads finished, exiting nicely" );
					m_Exiting = true;
				}
				else if( GetTime( ) - m_AllGamesFinishedTime >= 60 )
				{
					CONSOLE_Print( "[GHOST] waited 60 seconds for threads to finish, exiting anyway" );
					CONSOLE_Print( "[GHOST] there are " + UTIL_ToString( m_Callables.size( ) ) + " threads still in progress which will be terminated" );
					m_Exiting = true;
				}
			}
		}
	}

	// update callables

	boost::mutex::scoped_lock callablesLock( m_CallablesMutex );

	for( vector<CBaseCallable *> :: iterator i = m_Callables.begin( ); i != m_Callables.end( ); )
	{
		if( !(*i) )
		{
			// NULL presumably because we're using SQLite database with unimplemented database call
			// so just remove it from callables
			i = m_Callables.erase( i );
		}
		else if( (*i)->GetReady( ) )
		{
			m_DB->RecoverCallable( *i );
			delete *i;
			i = m_Callables.erase( i );
		}
		else
			++i;
	}
	
	callablesLock.unlock( );

	// create the GProxy++ reconnect listener

	if( m_Reconnect )
	{
		if( !m_ReconnectSocket )
		{
			bool Success = false;

			for( unsigned int i = 0; i < 50; i++ )
			{
				m_ReconnectSocket = new CTCPServer( );
				
				if( m_ReconnectSocket->Listen( m_BindAddress, m_ReconnectPort ) )
				{
					CONSOLE_Print( "[GHOST] listening for GProxy++ reconnects on port " + UTIL_ToString( m_ReconnectPort ) );
					Success = true;
					break;
				}
				else
				{
					CONSOLE_Print( "[GHOST] error listening for GProxy++ reconnects on port " + UTIL_ToString( m_ReconnectPort ) );
					delete m_ReconnectSocket;
					m_ReconnectSocket = NULL;
					m_ReconnectPort++;
				}
			}
			
			if( !Success )
			{
				CONSOLE_Print( "[GHOST] failed to listen for GProxy++ reconnects too many times, giving up" );
				m_Reconnect = false;
			}
		}
		else if( m_ReconnectSocket->HasError( ) )
		{
			CONSOLE_Print( "[GHOST] GProxy++ reconnect listener error (" + m_ReconnectSocket->GetErrorString( ) + ")" );
			delete m_ReconnectSocket;
			m_ReconnectSocket = NULL;
			m_Reconnect = false;
		}
	}
	
	// create the stream listener
	
	if( m_Stream )
	{
		if( !m_StreamSocket )
		{
			bool Success = false;

			for( unsigned int i = 0; i < 50; i++ )
			{
				m_StreamSocket = new CTCPServer( );
				
				if( m_StreamSocket->Listen( string( ), m_StreamPort ) )
				{
					CONSOLE_Print( "[GHOST] listening for streamers on port " + UTIL_ToString( m_StreamPort ) );
					Success = true;
					break;
				}
				else
				{
					CONSOLE_Print( "[GHOST] error listening for streamers on port " + UTIL_ToString( m_StreamPort ) );
					delete m_StreamSocket;
					m_StreamSocket = NULL;
					m_StreamPort++;
				}
			}
			
			if( !Success )
			{
				CONSOLE_Print( "[GHOST] failed to listen for streamers too many times, giving up" );
				m_Stream = false;
			}
		}
		else if( m_StreamSocket->HasError( ) )
		{
			CONSOLE_Print( "[GHOST] streamer listener error (" + m_StreamSocket->GetErrorString( ) + ")" );
			delete m_StreamSocket;
			m_StreamSocket = NULL;
			m_Stream = false;
		}
	}

	unsigned int NumFDs = 0;

	// take every socket we own and throw it in one giant select statement so we can block on all sockets

	int nfds = 0;
	fd_set fd;
	fd_set send_fd;
	FD_ZERO( &fd );
	FD_ZERO( &send_fd );

	// 1. all battle.net sockets

	for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		NumFDs += (*i)->SetFD( &fd, &send_fd, &nfds );

	// 2. the GProxy++ reconnect socket(s)

	if( m_Reconnect && m_ReconnectSocket )
	{
		m_ReconnectSocket->SetFD( &fd, &send_fd, &nfds );
                ++NumFDs;
	}

	for( vector<CTCPSocket *> :: iterator i = m_ReconnectSockets.begin( ); i != m_ReconnectSockets.end( ); ++i )
	{
		(*i)->SetFD( &fd, &send_fd, &nfds );
                ++NumFDs;
	}
	
	// 3. the streamer socket(s)
	
	if( m_Stream && m_StreamSocket )
	{
		m_StreamSocket->SetFD( &fd, &send_fd, &nfds );
		++NumFDs;
	}
	
	for( vector<CStreamPlayer *> :: iterator i = m_StreamPlayers.begin( ); i != m_StreamPlayers.end( ); ++i )
	{
		if( (*i)->GetSocket( ) )
		{
			(*i)->GetSocket( )->SetFD( &fd, &send_fd, &nfds );
			++NumFDs;
		}
	}

	// 4. the stage socket(s)
	
	for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); ++i )
	{
		if( (*i)->GetSocket( ) )
		{
			(*i)->GetSocket( )->SetFD( &fd, &send_fd, &nfds );
			++NumFDs;
		}
	}

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = usecBlock;

	struct timeval send_tv;
	send_tv.tv_sec = 0;
	send_tv.tv_usec = 0;

#ifdef WIN32
	select( 1, &fd, NULL, NULL, &tv );
	select( 1, NULL, &send_fd, NULL, &send_tv );
#else
	select( nfds + 1, &fd, NULL, NULL, &tv );
	select( nfds + 1, NULL, &send_fd, NULL, &send_tv );
#endif

	if( NumFDs == 0 )
	{
		// we don't have any sockets (i.e. we aren't connected to battle.net maybe due to a lost connection and there aren't any games running)
		// select will return immediately and we'll chew up the CPU if we let it loop so just sleep for 50ms to kill some time

		MILLISLEEP( 50 );
	}

	bool AdminExit = false;
	bool BNETExit = false;

	// update battle.net connections

	for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
	{
		if( (*i)->Update( &fd, &send_fd ) )
			BNETExit = true;
	}

	// update GProxy++ reliable reconnect sockets

	if( m_Reconnect && m_ReconnectSocket )
	{
		CTCPSocket *NewSocket = m_ReconnectSocket->Accept( &fd );

		if( NewSocket )
			m_ReconnectSockets.push_back( NewSocket );
	}

	for( vector<CTCPSocket *> :: iterator i = m_ReconnectSockets.begin( ); i != m_ReconnectSockets.end( ); )
	{
		if( (*i)->HasError( ) || !(*i)->GetConnected( ) || GetTime( ) - (*i)->GetLastRecv( ) >= 10 )
		{
			delete *i;
			i = m_ReconnectSockets.erase( i );
			continue;
		}

		(*i)->DoRecv( &fd );
		string *RecvBuffer = (*i)->GetBytes( );
		BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

		// a packet is at least 4 bytes

		if( Bytes.size( ) >= 4 )
		{
			if( Bytes[0] == GPS_HEADER_CONSTANT )
			{
				// bytes 2 and 3 contain the length of the packet

				uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

				if( Length >= 4 )
				{
					if( Bytes.size( ) >= Length )
					{
						if( Bytes[1] == CGPSProtocol :: GPS_RECONNECT && Length == 13 )
						{
							GProxyReconnector *Reconnector = new GProxyReconnector;
							Reconnector->PID = Bytes[4];
							Reconnector->ReconnectKey = UTIL_ByteArrayToUInt32( Bytes, false, 5 );
							Reconnector->LastPacket = UTIL_ByteArrayToUInt32( Bytes, false, 9 );
							Reconnector->PostedTime = GetTicks( );
							Reconnector->socket = (*i);

							// update the receive buffer
							*RecvBuffer = RecvBuffer->substr( Length );
							i = m_ReconnectSockets.erase( i );

							// post in the reconnects buffer and wait to see if a game thread will pick it up
							boost::mutex::scoped_lock lock( m_ReconnectMutex );
							m_PendingReconnects.push_back( Reconnector );
							lock.unlock();
							continue;
						}
						
						else
						{
							(*i)->PutBytes( m_GPSProtocol->SEND_GPSS_REJECT( REJECTGPS_INVALID ) );
							(*i)->DoSend( &send_fd );
							delete *i;
							i = m_ReconnectSockets.erase( i );
							continue;
						}
					}
				}
				else
				{
					(*i)->PutBytes( m_GPSProtocol->SEND_GPSS_REJECT( REJECTGPS_INVALID ) );
					(*i)->DoSend( &send_fd );
					delete *i;
					i = m_ReconnectSockets.erase( i );
					continue;
				}
			}
			else
			{
				(*i)->PutBytes( m_GPSProtocol->SEND_GPSS_REJECT( REJECTGPS_INVALID ) );
				(*i)->DoSend( &send_fd );
				delete *i;
				i = m_ReconnectSockets.erase( i );
				continue;
			}
		}

		(*i)->DoSend( &send_fd );
		++i;
	}

	// delete any old pending reconnects that have not been handled by games
	if( !m_PendingReconnects.empty( ) ) {
		boost::mutex::scoped_lock lock( m_ReconnectMutex );
	
		for( vector<GProxyReconnector *> :: iterator i = m_PendingReconnects.begin( ); i != m_PendingReconnects.end( ); )
		{
			if( GetTicks( ) - (*i)->PostedTime > 1500 )
			{
				(*i)->socket->PutBytes( m_GPSProtocol->SEND_GPSS_REJECT( REJECTGPS_NOTFOUND ) );
				(*i)->socket->DoSend( &send_fd );
				delete (*i)->socket;
				delete (*i);
				i = m_PendingReconnects.erase( i );
				continue;
			}
			
			i++;
		}
	
		lock.unlock();
	}
	
	if( m_Stage )
	{
		// update staging players
		// also add new ones from the DoAdd vector

		for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); )
		{
			if( (*i)->Update( &fd ) )
			{
				if( (*i)->GetSocket( ) )
					(*i)->GetSocket( )->DoSend( &send_fd );

				delete *i;
				i = m_StagePlayers.erase( i );
			}
			else
			{
				if( (*i)->GetSocket( ) )
					(*i)->GetSocket( )->DoSend( &send_fd );

				++i;
			}
		}
	
		if( !m_StageDoAdd.empty( ) )
		{
			boost::mutex::scoped_lock lock( m_StageMutex );
		
			for( vector<CTCPSocket *> :: iterator i = m_StageDoAdd.begin( ); i != m_StageDoAdd.end( ); ++i )
			{
				m_StagePlayers.push_back( new CStagePlayer( m_GameProtocol, *i, this ) );
			}
		
			m_StageDoAdd.clear( );
			lock.unlock( );
		}
		
		if( !m_DoSpoofAdd.empty( ) )
		{
			boost::mutex::scoped_lock lock( m_StageMutex );
			
			for( vector<QueuedSpoofAdd> :: iterator i = m_DoSpoofAdd.begin( ); i != m_DoSpoofAdd.end( ); ++i )
			{
				string SpoofName = i->name;
				transform( SpoofName.begin( ), SpoofName.end( ), SpoofName.begin( ), (int(*)(int))tolower );
				
				for( vector<CStagePlayer *> :: iterator j = m_StagePlayers.begin( ); j != m_StagePlayers.end( ); ++j )
				{
					string StageName = (*j)->GetName( );
					transform( StageName.begin( ), StageName.end( ), StageName.begin( ), (int(*)(int))tolower );
					
					if( StageName == SpoofName && i->server == (*j)->GetRealm( ) )
						(*j)->SetChecked( );
				}
			}
			
			m_DoSpoofAdd.clear( );
			lock.unlock( );
		}
	}

	// update stream sockets

	if( m_Stream && m_StreamSocket )
	{
		CTCPSocket *NewSocket = m_StreamSocket->Accept( &fd );

		if( NewSocket )
		{
			if( !CheckDeny( NewSocket->GetIPString( ) ) )
			{
				if( m_TCPNoDelay )
					NewSocket->SetNoDelay( true );

				DenyIP( NewSocket->GetIPString( ), 60000, "streamer connected" );
				m_StreamPlayers.push_back( new CStreamPlayer( m_GameProtocol, NewSocket, this ) );
			}
			else
			{
				CONSOLE_Print( "[STREAM] rejected connection from [" + NewSocket->GetIPString( ) + "] due to blacklist" );
				delete NewSocket;
			}
		}
	}

	for( vector<CStreamPlayer *> :: iterator i = m_StreamPlayers.begin( ); i != m_StreamPlayers.end( ); )
	{
		if( (*i)->Update( &fd ) )
		{
			if( (*i)->GetSocket( ) )
				(*i)->GetSocket( )->DoSend( &send_fd );

			delete *i;
			i = m_StreamPlayers.erase( i );
		}
		else if( (*i)->m_Game )
		{
			boost::mutex::scoped_lock lock( (*i)->m_Game->m_StreamMutex );
			(*i)->m_Game->m_DoAddStreamPlayer.push( *i );
			lock.unlock( );
			
			i = m_StreamPlayers.erase( i );
		}
		else
		{
			if( (*i)->GetSocket( ) )
				(*i)->GetSocket( )->DoSend( &send_fd );

			++i;
		}
	}
	
	// try to create a new game every ten seconds
	
	if( m_Stage && GetTime( ) - m_LastStageTime >= 10 && !m_AutoHostMap.empty( ) )
	{
		m_LastStageTime = GetTime( );
		
		CONSOLE_Print( "[GHOST] Trying to create a game..." );
		uint32_t BestPlayers = 0; // best number of players found
		
		// loop over all the possible scores that we want to try
	
		int minScore = 800;
		int maxScore = 1300;
	
		for( int score = minScore; score <= maxScore; score += 10 )
		{
			// type indicates whether these is a minimum, maximum, or in-between score
			int scoreType = 0;
		
			if( score == minScore )
				scoreType = -1;
			else if( score == maxScore )
				scoreType = 1;
		
			// we want to find ten players, each with a different PID
			CStagePlayer *FoundPlayers[12]; // target PID is index plus one
			int NumPlayersFound = 0;
		
			for( int i = 0; i < 12; i++ )
				FoundPlayers[i] = NULL;
		
			// loop from oldest to newest players
			// so this way players who have been here longer are favored
		
			for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); ++i )
			{
				if( (*i)->GetReady( ) && !FoundPlayers[(*i)->GetPID( ) - 1] && (*i)->IsScoreAcceptable( score, scoreType ) )
				{
					FoundPlayers[(*i)->GetPID( ) - 1] = *i;
					NumPlayersFound++;
				
					if( NumPlayersFound == 2 )
						break;
				}
			}
		
			if( NumPlayersFound > BestPlayers )
				BestPlayers = NumPlayersFound;
		
			if( NumPlayersFound == 2 )
			{
				// first, remove these stage players from our vector
				// TODO: use better data structure to make this step more efficient
			
				CONSOLE_Print( "[GHOST] Found " + UTIL_ToString( NumPlayersFound ) + " players, initializing game at score=" + UTIL_ToString( score ) );
			
				for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); )
				{
					if( *i == FoundPlayers[0] || *i == FoundPlayers[1] || *i == FoundPlayers[2] || *i == FoundPlayers[3] || *i == FoundPlayers[4] || *i == FoundPlayers[5] || *i == FoundPlayers[6] || *i == FoundPlayers[7] || *i == FoundPlayers[8] || *i == FoundPlayers[9] || *i == FoundPlayers[10] || *i == FoundPlayers[11] )
					{
						i = m_StagePlayers.erase( i );
					}
					else
						++i;
				}
			
				// associate this with a new game
				
				string Gamename = m_AutoHostGameName + " #" + UTIL_ToString( m_HostCounter++ );
				CGame *NewGame = new CGame( this, m_AutoHostMap[0], NULL, 6112, GAME_PUBLIC, Gamename, "", "", "" );
				BroadcastChat( "ENT", "The game [" + Gamename + "] has started, with score centered around [" + UTIL_ToString( score ) + "]." );
			
				for( int i = 0; i < 12; i++ )
				{
					if( FoundPlayers[i] )
					{
						FoundPlayers[i]->RemoveVirtual( ); // remove the virtual host
						NewGame->AddPlayerFast( FoundPlayers[i] );
						delete FoundPlayers[i];
					}
				}
				
				NewGame->EventGameStarted( );
				m_Games.push_back( NewGame );
				boost::thread(&CBaseGame::loop, NewGame);
				break;
			}
		}
		
		CONSOLE_Print( "[GHOST] Best number of players found: " + UTIL_ToString( BestPlayers ) );
	}

	// autohost

	if( !m_AutoHostGameName.empty( ) && m_AutoHostMaximumGames != 0 && m_AutoHostAutoStartPlayers != 0 && GetTime( ) - m_LastAutoHostTime >= 10 && !m_BNETs.empty( ) && m_BNETs[0]->GetOutPacketsQueued( ) <= 1 )
	{
		// copy all the checks from CGHost :: CreateGame here because we don't want to spam the chat when there's an error
		// instead we fail silently and try again soon
		boost::mutex::scoped_lock gamesLock( m_GamesMutex );

		if( !m_ExitingNice && m_Enabled && !m_CurrentGame && m_Games.size( ) < m_MaxGames && m_Games.size( ) < m_AutoHostMaximumGames && !m_AutoHostMap.empty( ) )
		{
			// pick random map to autohost if we have more than one
			CMap *AutoHostMap = m_AutoHostMap[rand( ) % m_AutoHostMap.size( )];
			
			if( AutoHostMap->GetValid( ) )
			{
				string GameName = ( AutoHostMap->GetGameName( ).empty( ) ? m_AutoHostGameName : AutoHostMap->GetGameName( ) ) + " #" + UTIL_ToString( m_HostCounter % 100 );

				if( GameName.size( ) <= 31 )
				{
					// CreateGame handles its own locking on games mutex, so release lock here
					gamesLock.unlock( );
					CreateGame( AutoHostMap, GAME_PUBLIC, false, GameName, m_AutoHostOwner, m_AutoHostOwner, m_AutoHostServer, false );

					if( m_CurrentGame )
					{
						m_CurrentGame->SetAutoStartPlayers( AutoHostMap->GetStartPlayers( ) == 0 ? m_AutoHostAutoStartPlayers : AutoHostMap->GetStartPlayers( ) );

						if( m_AutoHostMatchMaking )
						{
							if( !AutoHostMap->GetMapMatchMakingCategory( ).empty( ) )
							{
								if( !( AutoHostMap->GetMapOptions( ) & MAPOPT_FIXEDPLAYERSETTINGS ) )
									CONSOLE_Print( "[GHOST] autohostmm - map_matchmakingcategory [" + AutoHostMap->GetMapMatchMakingCategory( ) + "] found but matchmaking can only be used with fixed player settings, matchmaking disabled" );
								else
								{
									CONSOLE_Print( "[GHOST] autohostmm - map_matchmakingcategory [" + AutoHostMap->GetMapMatchMakingCategory( ) + "] found, matchmaking enabled" );

									m_CurrentGame->SetMatchMaking( true );
									m_CurrentGame->SetMinimumScore( m_AutoHostMinimumScore );
									m_CurrentGame->SetMaximumScore( m_AutoHostMaximumScore );
								}
							}
							else
								CONSOLE_Print( "[GHOST] autohostmm - map_matchmakingcategory not found, matchmaking disabled" );
						}
					}
				}
				else
				{
					CONSOLE_Print( "[GHOST] stopped auto hosting, next game name [" + GameName + "] is too long (the maximum is 31 characters)" );
					m_AutoHostGameName.clear( );
					m_AutoHostOwner.clear( );
					m_AutoHostServer.clear( );
					m_AutoHostMaximumGames = 0;
					m_AutoHostAutoStartPlayers = 0;
					m_AutoHostMatchMaking = false;
					m_AutoHostMinimumScore = 0.0;
					m_AutoHostMaximumScore = 0.0;
				}
			}
			else
			{
				CONSOLE_Print( "[GHOST] stopped auto hosting, map config file [" + AutoHostMap->GetCFGFile( ) + "] is invalid" );
				m_AutoHostGameName.clear( );
				m_AutoHostOwner.clear( );
				m_AutoHostServer.clear( );
				m_AutoHostMaximumGames = 0;
				m_AutoHostAutoStartPlayers = 0;
				m_AutoHostMatchMaking = false;
				m_AutoHostMinimumScore = 0.0;
				m_AutoHostMaximumScore = 0.0;
			}
		}

		m_LastAutoHostTime = GetTime( );
	}

	// refresh spoof list

	if( !m_CallableSpoofList && GetTime( ) - m_LastSpoofRefreshTime >= 1200 )
		m_CallableSpoofList = m_DB->ThreadedSpoofList( );

	if( m_CallableSpoofList && m_CallableSpoofList->GetReady( ) )
	{
		boost::mutex::scoped_lock lock( m_SpoofMutex );
		
		m_SpoofList = m_CallableSpoofList->GetResult( );
		m_DB->RecoverCallable( m_CallableSpoofList );
		delete m_CallableSpoofList;
		m_CallableSpoofList = NULL;
		m_LastSpoofRefreshTime = GetTime( );
		
		lock.unlock( );
	}
	
	//clean the deny table every two minutes
	
	if( GetTime( ) - m_LastDenyCleanTime >= 120 )
	{
		boost::mutex::scoped_lock lock( m_DenyMutex );
		
		for( map<string, DenyInfo>::iterator i = m_DenyIP.begin( ); i != m_DenyIP.end( ); )
		{
			if( GetTicks( ) - i->second.Time > 60000 + i->second.Duration )
				m_DenyIP.erase(i++);
			else
				++i;
		}
		
		lock.unlock( );
		m_LastDenyCleanTime = GetTime( );
	}

	//try to handle an existing game request
	if( m_MapGameCreateRequest && GetTicks( ) - m_MapGameCreateRequestTicks > 1000 )
	{
		boost::mutex::scoped_lock lock( m_MapMutex, boost::try_to_lock );

		if( lock )
		{
			CreateGame( m_Map, m_MapGameCreateRequest->gameState, m_MapGameCreateRequest->saveGame, m_MapGameCreateRequest->gameName, m_MapGameCreateRequest->ownerName, m_MapGameCreateRequest->creatorName, m_MapGameCreateRequest->creatorServer, m_MapGameCreateRequest->whisper );
			delete m_MapGameCreateRequest;
			m_MapGameCreateRequest = NULL;
		}
	}

	//flush log file every second
	if( GetTicks( ) - gLogLastTicks > 1000 )
		CONSOLE_Flush( );

	return m_Exiting || AdminExit || BNETExit;
}

void CGHost :: EventBNETConnecting( CBNET *bnet )
{
}

void CGHost :: EventBNETConnected( CBNET *bnet )
{
}

void CGHost :: EventBNETDisconnected( CBNET *bnet )
{
}

void CGHost :: EventBNETLoggedIn( CBNET *bnet )
{
}

void CGHost :: EventBNETGameRefreshed( CBNET *bnet )
{
	boost::mutex::scoped_lock lock( m_GamesMutex );
	if( m_CurrentGame )
		m_CurrentGame->EventGameRefreshed( bnet->GetServer( ) );
	
	lock.unlock( );
}

void CGHost :: EventBNETGameRefreshFailed( CBNET *bnet )
{
	boost::mutex::scoped_lock lock( m_GamesMutex );
	
	if( m_CurrentGame )
	{
		if( !m_CurrentGame->GetRefreshError( ) )
		{
			for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
			{
				(*i)->QueueChatCommand( m_Language->UnableToCreateGameTryAnotherName( bnet->GetServer( ), m_CurrentGame->GetGameName( ) ) );

				if( (*i)->GetServer( ) == m_CurrentGame->GetCreatorServer( ) )
					(*i)->QueueChatCommand( m_Language->UnableToCreateGameTryAnotherName( bnet->GetServer( ), m_CurrentGame->GetGameName( ) ), m_CurrentGame->GetCreatorName( ), true );
			}
		}

		boost::mutex::scoped_lock sayLock( m_CurrentGame->m_SayGamesMutex );
		m_CurrentGame->m_DoSayGames.push_back( m_Language->UnableToCreateGameTryAnotherName( bnet->GetServer( ), m_CurrentGame->GetGameName( ) ) );
		sayLock.unlock( );

		// we take the easy route and simply close the lobby if a refresh fails
		// it's possible at least one refresh succeeded and therefore the game is still joinable on at least one battle.net (plus on the local network) but we don't keep track of that
		// we only close the game if it has no players since we support game rehosting (via !priv and !pub in the lobby)

		if( m_CurrentGame->GetNumHumanPlayers( ) == 0 )
			m_CurrentGame->SetExiting( true );

		m_CurrentGame->SetRefreshError( true );
	}
	
	lock.unlock( );
}

void CGHost :: EventBNETConnectTimedOut( CBNET *bnet )
{
}

void CGHost :: EventBNETWhisper( CBNET *bnet, string user, string message )
{
}

void CGHost :: EventBNETChat( CBNET *bnet, string user, string message )
{
}

void CGHost :: EventBNETEmote( CBNET *bnet, string user, string message )
{
}

void CGHost :: EventGameDeleted( CBaseGame *game )
{
	if( m_AutoHostMaximumGames == 0 )
	{
		for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			(*i)->QueueChatCommand( m_Language->GameIsOver( game->GetDescription( ) ) );

			if( (*i)->GetServer( ) == game->GetCreatorServer( ) )
				(*i)->QueueChatCommand( m_Language->GameIsOver( game->GetDescription( ) ), game->GetCreatorName( ), true );
		}
	}
}

string CGHost :: GetSpoofName( string name )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	
	boost::mutex::scoped_lock lock( m_SpoofMutex );
	if( m_SpoofList.count( name ) > 0 )
		return m_SpoofList[name];
	lock.unlock( );

	return string( );
}

void CGHost :: ClearAutoHostMap( )
{
	for( vector<CMap *> :: iterator i = m_AutoHostMap.begin( ); i != m_AutoHostMap.end( ); ++i )
		delete *i;
	
	m_AutoHostMap.clear( );
}

void CGHost :: ReloadConfigs( )
{
	CConfig CFG;
	CFG.Read( "default.cfg" );
	CFG.Read( gCFGFile );
	SetConfigs( &CFG );
}

void CGHost :: SetConfigs( CConfig *CFG )
{
	// this doesn't set EVERY config value since that would potentially require reconfiguring the battle.net connections
	// it just set the easily reloadable values

	m_LanguageFile = CFG->GetString( "bot_language", "language.cfg" );
	delete m_Language;
	m_Language = new CLanguage( m_LanguageFile );
	m_Warcraft3Path = UTIL_AddPathSeperator( CFG->GetString( "bot_war3path", "C:\\Program Files\\Warcraft III\\" ) );
	m_BindAddress = CFG->GetString( "bot_bindaddress", string( ) );
	m_ReconnectWaitTime = CFG->GetInt( "bot_reconnectwaittime", 3 );
	m_MaxGames = CFG->GetInt( "bot_maxgames", 5 );
	string BotCommandTrigger = CFG->GetString( "bot_commandtrigger", "!" );

	if( BotCommandTrigger.empty( ) )
		BotCommandTrigger = "!";

	m_CommandTrigger = BotCommandTrigger[0];
	m_MapCFGPath = UTIL_AddPathSeperator( CFG->GetString( "bot_mapcfgpath", string( ) ) );
	m_SaveGamePath = UTIL_AddPathSeperator( CFG->GetString( "bot_savegamepath", string( ) ) );
	m_MapPath = UTIL_AddPathSeperator( CFG->GetString( "bot_mappath", string( ) ) );
	m_SaveReplays = CFG->GetInt( "bot_savereplays", 0 ) == 0 ? false : true;
	m_ReplayPath = UTIL_AddPathSeperator( CFG->GetString( "bot_replaypath", string( ) ) );
	m_VirtualHostName = CFG->GetString( "bot_virtualhostname", "|cFF4080C0GHost" );
	m_HideIPAddresses = CFG->GetInt( "bot_hideipaddresses", 0 ) == 0 ? false : true;
	m_CheckMultipleIPUsage = CFG->GetInt( "bot_checkmultipleipusage", 1 ) == 0 ? false : true;

	if( m_VirtualHostName.size( ) > 15 )
	{
		m_VirtualHostName = "|cFF4080C0GHost";
		CONSOLE_Print( "[GHOST] warning - bot_virtualhostname is longer than 15 characters, using default virtual host name" );
	}

	m_SpoofChecks = CFG->GetInt( "bot_spoofchecks", 2 );
	m_RequireSpoofChecks = CFG->GetInt( "bot_requirespoofchecks", 0 ) == 0 ? false : true;
	m_ReserveAdmins = CFG->GetInt( "bot_reserveadmins", 1 ) == 0 ? false : true;
	m_RefreshMessages = CFG->GetInt( "bot_refreshmessages", 0 ) == 0 ? false : true;
	m_AutoLock = CFG->GetInt( "bot_autolock", 0 ) == 0 ? false : true;
	m_AutoSave = CFG->GetInt( "bot_autosave", 0 ) == 0 ? false : true;
	m_AllowDownloads = CFG->GetInt( "bot_allowdownloads", 0 );
	m_PingDuringDownloads = CFG->GetInt( "bot_pingduringdownloads", 0 ) == 0 ? false : true;
	m_MaxDownloaders = CFG->GetInt( "bot_maxdownloaders", 3 );
	m_MaxDownloadSpeed = CFG->GetInt( "bot_maxdownloadspeed", 100 );
	m_LCPings = CFG->GetInt( "bot_lcpings", 1 ) == 0 ? false : true;
	m_AutoKickPing = CFG->GetInt( "bot_autokickping", 400 );
	m_IPBlackListFile = CFG->GetString( "bot_ipblacklistfile", "ipblacklist.txt" );
	m_LobbyTimeLimit = CFG->GetInt( "bot_lobbytimelimit", 10 );
	m_Latency = CFG->GetInt( "bot_latency", 100 );
    m_SyncLimit = CFG->GetInt( "bot_synclimit", 50 );
	m_VoteKickAllowed = CFG->GetInt( "bot_votekickallowed", 1 ) == 0 ? false : true;
	m_VoteStartAllowed = CFG->GetInt( "bot_votestartallowed", 1 ) == 0 ? false : true;
	m_VoteStartAutohostOnly = CFG->GetInt( "bot_votestartautohostonly", 1 ) == 0 ? false : true;
	m_VoteStartMinPlayers = CFG->GetInt( "bot_votestartplayers", 8 );
	m_VoteKickPercentage = CFG->GetInt( "bot_votekickpercentage", 100 );

	if( m_VoteKickPercentage > 100 )
	{
		m_VoteKickPercentage = 100;
		CONSOLE_Print( "[GHOST] warning - bot_votekickpercentage is greater than 100, using 100 instead" );
	}

	m_MOTDFile = CFG->GetString( "bot_motdfile", "motd.txt" );
	m_GameLoadedFile = CFG->GetString( "bot_gameloadedfile", "gameloaded.txt" );
	m_GameOverFile = CFG->GetString( "bot_gameoverfile", "gameover.txt" );
	m_LocalAdminMessages = CFG->GetInt( "bot_localadminmessages", 1 ) == 0 ? false : true;
	m_TCPNoDelay = CFG->GetInt( "tcp_nodelay", 0 ) == 0 ? false : true;
	m_MatchMakingMethod = CFG->GetInt( "bot_matchmakingmethod", 1 );
    m_MapGameType = CFG->GetUInt32( "bot_mapgametype", 21569728 );
    m_FastReconnect = CFG->GetInt( "bot_fastreconnect", 0 ) == 0 ? false : true;
    m_CloseSinglePlayer = CFG->GetInt( "bot_closesingleplayer", 1 ) == 0 ? false : true;
    m_AMH = CFG->GetInt( "bot_amh", 0 ) == 0 ? false : true;
    
    m_Stream = CFG->GetInt( "bot_stream", 0 ) == 0 ? false : true;
	m_StreamPort = CFG->GetInt( "bot_streamport", m_HostPort * 2 );
	m_StreamLimit = CFG->GetInt( "bot_streamlimit", 300 );
	
    m_Stage = CFG->GetInt( "bot_stage", 0 ) == 0 ? false : true;
}

void CGHost :: ExtractScripts( )
{
	string PatchMPQFileName = m_Warcraft3Path + "War3Patch.mpq";
	HANDLE PatchMPQ;

	if( SFileOpenArchive( PatchMPQFileName.c_str( ), 0, MPQ_OPEN_FORCE_MPQ_V1, &PatchMPQ ) )
	{
		CONSOLE_Print( "[GHOST] loading MPQ file [" + PatchMPQFileName + "]" );
		HANDLE SubFile;

		// common.j

		if( SFileOpenFileEx( PatchMPQ, "Scripts\\common.j", 0, &SubFile ) )
		{
			uint32_t FileLength = SFileGetFileSize( SubFile, NULL );

			if( FileLength > 0 && FileLength != 0xFFFFFFFF )
			{
				char *SubFileData = new char[FileLength];
				DWORD BytesRead = 0;

				if( SFileReadFile( SubFile, SubFileData, FileLength, &BytesRead ) )
				{
					CONSOLE_Print( "[GHOST] extracting Scripts\\common.j from MPQ file to [" + m_MapCFGPath + "common.j]" );
					UTIL_FileWrite( m_MapCFGPath + "common.j", (unsigned char *)SubFileData, BytesRead );
				}
				else
					CONSOLE_Print( "[GHOST] warning - unable to extract Scripts\\common.j from MPQ file" );

				delete [] SubFileData;
			}

			SFileCloseFile( SubFile );
		}
		else
			CONSOLE_Print( "[GHOST] couldn't find Scripts\\common.j in MPQ file" );

		// blizzard.j

		if( SFileOpenFileEx( PatchMPQ, "Scripts\\blizzard.j", 0, &SubFile ) )
		{
			uint32_t FileLength = SFileGetFileSize( SubFile, NULL );

			if( FileLength > 0 && FileLength != 0xFFFFFFFF )
			{
				char *SubFileData = new char[FileLength];
				DWORD BytesRead = 0;

				if( SFileReadFile( SubFile, SubFileData, FileLength, &BytesRead ) )
				{
					CONSOLE_Print( "[GHOST] extracting Scripts\\blizzard.j from MPQ file to [" + m_MapCFGPath + "blizzard.j]" );
					UTIL_FileWrite( m_MapCFGPath + "blizzard.j", (unsigned char *)SubFileData, BytesRead );
				}
				else
					CONSOLE_Print( "[GHOST] warning - unable to extract Scripts\\blizzard.j from MPQ file" );

				delete [] SubFileData;
			}

			SFileCloseFile( SubFile );
		}
		else
			CONSOLE_Print( "[GHOST] couldn't find Scripts\\blizzard.j in MPQ file" );

		SFileCloseArchive( PatchMPQ );
	}
	else
		CONSOLE_Print( "[GHOST] warning - unable to load MPQ file [" + PatchMPQFileName + "] - error code " + UTIL_ToString( GetLastError( ) ) );
}

void CGHost :: CreateGame( CMap *map, unsigned char gameState, bool saveGame, string gameName, string ownerName, string creatorName, string creatorServer, bool whisper )
{
	if( !m_Enabled )
	{
		for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			if( (*i)->GetServer( ) == creatorServer )
				(*i)->QueueChatCommand( m_Language->UnableToCreateGameDisabled( gameName ), creatorName, whisper );
		}

		return;
	}
	
	gameName.erase( remove_if( gameName.begin( ), gameName.end( ), UTIL_IsNonPrintable ), gameName.end( ) );

	if( gameName.size( ) > 31 || gameName.size( ) < 3 )
	{
		for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			if( (*i)->GetServer( ) == creatorServer )
				(*i)->QueueChatCommand( m_Language->UnableToCreateGameNameTooLong( gameName ), creatorName, whisper );
		}

		return;
	}

	if( !map->GetValid( ) )
	{
		for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			if( (*i)->GetServer( ) == creatorServer )
				(*i)->QueueChatCommand( m_Language->UnableToCreateGameInvalidMap( gameName ), creatorName, whisper );
		}

		return;
	}

	if( saveGame )
	{
		if( !m_SaveGame->GetValid( ) )
		{
                        for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
			{
				if( (*i)->GetServer( ) == creatorServer )
					(*i)->QueueChatCommand( m_Language->UnableToCreateGameInvalidSaveGame( gameName ), creatorName, whisper );
			}

			return;
		}

		string MapPath1 = m_SaveGame->GetMapPath( );
		string MapPath2 = map->GetMapPath( );
		transform( MapPath1.begin( ), MapPath1.end( ), MapPath1.begin( ), (int(*)(int))tolower );
		transform( MapPath2.begin( ), MapPath2.end( ), MapPath2.begin( ), (int(*)(int))tolower );

		if( MapPath1 != MapPath2 )
		{
			CONSOLE_Print( "[GHOST] path mismatch, saved game path is [" + MapPath1 + "] but map path is [" + MapPath2 + "]" );

                        for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
			{
				if( (*i)->GetServer( ) == creatorServer )
					(*i)->QueueChatCommand( m_Language->UnableToCreateGameSaveGameMapMismatch( gameName ), creatorName, whisper );
			}

			return;
		}

		if( m_EnforcePlayers.empty( ) )
		{
                        for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
			{
				if( (*i)->GetServer( ) == creatorServer )
					(*i)->QueueChatCommand( m_Language->UnableToCreateGameMustEnforceFirst( gameName ), creatorName, whisper );
			}

			return;
		}
	}

	boost::mutex::scoped_lock lock( m_GamesMutex );
	
	if( m_CurrentGame )
	{
		for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			if( (*i)->GetServer( ) == creatorServer )
				(*i)->QueueChatCommand( m_Language->UnableToCreateGameAnotherGameInLobby( gameName, m_CurrentGame->GetDescription( ) ), creatorName, whisper );
		}

		return;
	}

	if( m_Games.size( ) >= m_MaxGames )
	{
		for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			if( (*i)->GetServer( ) == creatorServer )
				(*i)->QueueChatCommand( m_Language->UnableToCreateGameMaxGamesReached( gameName, UTIL_ToString( m_MaxGames ) ), creatorName, whisper );
		}

		return;
	}

	CONSOLE_Print( "[GHOST] creating game [" + gameName + "]" );

	if( saveGame )
		m_CurrentGame = new CGame( this, map, m_SaveGame, m_HostPort, gameState, gameName, ownerName, creatorName, creatorServer );
	else
		m_CurrentGame = new CGame( this, map, NULL, m_HostPort, gameState, gameName, ownerName, creatorName, creatorServer );

	// todotodo: check if listening failed and report the error to the user

	if( m_SaveGame )
	{
		m_CurrentGame->SetEnforcePlayers( m_EnforcePlayers );
		m_EnforcePlayers.clear( );
	}

	for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
	{
		if( m_AutoHostMaximumGames == 0 )
		{
			if( whisper && (*i)->GetServer( ) == creatorServer )
			{
				// note that we send this whisper only on the creator server

				if( gameState == GAME_PRIVATE )
					(*i)->QueueChatCommand( m_Language->CreatingPrivateGame( gameName, ownerName ), creatorName, whisper );
				else if( gameState == GAME_PUBLIC )
					(*i)->QueueChatCommand( m_Language->CreatingPublicGame( gameName, ownerName ), creatorName, whisper );
			}
			else
			{
				// note that we send this chat message on all other bnet servers

				if( gameState == GAME_PRIVATE )
					(*i)->QueueChatCommand( m_Language->CreatingPrivateGame( gameName, ownerName ) );
				else if( gameState == GAME_PUBLIC )
					(*i)->QueueChatCommand( m_Language->CreatingPublicGame( gameName, ownerName ) );
			}
		}

		if( saveGame )
			(*i)->QueueGameCreate( gameState, gameName, string( ), map, m_SaveGame, m_CurrentGame->GetHostCounter( ) );
		else
			(*i)->QueueGameCreate( gameState, gameName, string( ), map, NULL, m_CurrentGame->GetHostCounter( ) );
	}

	// if we're creating a private game we don't need to send any game refresh messages so we can rejoin the chat immediately
	// unfortunately this doesn't work on PVPGN servers because they consider an enterchat message to be a gameuncreate message when in a game
	// so don't rejoin the chat if we're using PVPGN

	if( gameState == GAME_PRIVATE )
	{
                for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
		{
			if( (*i)->GetPasswordHashType( ) != "pvpgn" )
				(*i)->QueueEnterChat( );
		}
	}

	// hold friends and/or clan members

	for( vector<CBNET *> :: iterator i = m_BNETs.begin( ); i != m_BNETs.end( ); ++i )
	{
		if( (*i)->GetHoldFriends( ) )
			(*i)->HoldFriends( m_CurrentGame );

		if( (*i)->GetHoldClan( ) )
			(*i)->HoldClan( m_CurrentGame );
	}
	
	// start the game thread
	boost::thread(&CBaseGame::loop, m_CurrentGame);
	CONSOLE_Print("[GameThread] Made new game thread");
}

void CGHost :: DenyIP( string ip, uint32_t duration, string reason )
{
	if( IsLocal( ip ) )
		return;
	
	CONSOLE_Print( "[DENY] Denying connections from " + ip + " for " + UTIL_ToString( duration ) + " milliseconds: " + reason );
	
	// check to see if already in table
	
	boost::mutex::scoped_lock lock( m_DenyMutex );
	
	if( m_DenyIP.count( ip ) == 0 )
	{
		DenyInfo Info;
		Info.Time = GetTicks( );
		Info.Duration = duration;
		Info.Count = 0;
		
		m_DenyIP[ip] = Info;
	}
	
	else
	{
		// only add if new ending time is greater than last ending time
		if( duration >= m_DenyIP[ip].Duration || GetTicks( ) - m_DenyIP[ip].Time > m_DenyIP[ip].Duration - duration ) {
			// increment deny count if necessary
			if( GetTicks( ) - m_DenyIP[ip].Time < 60000 )
			{
				m_DenyIP[ip].Count++;
				
				if( m_DenyIP[ip].Count > 20 )
				{
					duration = 1200000;
					CONSOLE_Print( "[DENY] Extending deny due to high deny count " );
					m_DenyIP[ip].Count = 0;
				}
			}
			
			else
				m_DenyIP[ip].Count = 0;
			
			m_DenyIP[ip].Time = GetTicks( );
			m_DenyIP[ip].Duration = duration;
		}
	}
	
	lock.unlock( );
}

bool CGHost :: CheckDeny( string ip ) {
	boost::mutex::scoped_lock lock( m_DenyMutex );
	
	if( m_DenyIP.count( ip ) == 0 )
		return false;
	else
	{
		if( GetTicks( ) - m_DenyIP[ip].Time < m_DenyIP[ip].Duration )
			return true;
		else
		{
			// delete stale entries only, so that we can use DenyCount properly
			if( GetTicks( ) - m_DenyIP[ip].Time > 60000 + m_DenyIP[ip].Duration )
			{
				m_DenyIP.erase( ip );
			}
			
			return false;
		}
	}
}

bool CGHost :: FlameCheck( string message )
{
	transform( message.begin( ), message.end( ), message.begin( ), (int(*)(int))tolower );
	
	for( int i = 0; i < m_FlameTriggers.size(); i++ )
	{
		if( message.find( m_FlameTriggers[i] ) != string :: npos )
			return true;
	}
	
	return false;
}

bool CGHost :: IsLocal( string ip )
{
	// multiple local IP's are space separated

	stringstream SS;
	string s;
	SS << m_LocalIPs;

	while( !SS.eof( ) )
	{
		SS >> s;

		if( ip == s )
			return true;
	}

	return false;
}

uint32_t CGHost :: CountStagePlayers( )
{
	uint32_t Count = 0;
	
	for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); ++i )
	{
		if( (*i)->GetReady( ) )
			Count++;
	}
	
	return Count;
}

void CGHost :: BroadcastChat( string name, string message )
{
	string overallMessage = message;
	
	if( !name.empty( ) )
		overallMessage = "[" + name + "]: " + message;
	
	CONSOLE_Print( "[GHOST] [BROADCAST] " + overallMessage );
	
	for( vector<CStagePlayer *> :: iterator i = m_StagePlayers.begin( ); i != m_StagePlayers.end( ); ++i )
	{
		if( (*i)->GetReady( ) )
			(*i)->SendChat( overallMessage, false );
	}
}

void CGHost :: AsynchronousMapLoad( CConfig *CFG, string nCFGFile )
{
	boost::thread(boost::bind(&CGHost::AsynchronousMapLoadHelper, this, CFG, nCFGFile));
}

void CGHost :: AsynchronousMapLoadHelper( CConfig *CFG, string nCFGFile )
{
	boost::mutex::scoped_lock lock( m_MapMutex, boost::try_to_lock );

	if( lock )
	{
		CONSOLE_Print( "[GHOST] AsynchronousMapLoad: loading from [" + nCFGFile + "]" );
		m_Map->Load( CFG, nCFGFile );
	}
	else
	{
		CONSOLE_Print( "[GHOST] AsynchronousMapLoad: failed to acquire lock" );
	}

	delete CFG; // we are responsible for handling the configuration
}

string CGHost :: HostNameLookup( string ip )
{
	//try to find in cache first
	boost::mutex::scoped_lock lockFind( m_HostNameCacheMutex );
	for( deque<HostNameInfo> :: iterator i = m_HostNameCache.begin( ); i != m_HostNameCache.end( ); i++ )
	{
		if( i->ip == ip )
			return i->hostname;
	}
	lockFind.unlock( );

	//couldn't find, so attempt to do the lookup
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;

	if( ( sin.sin_addr.s_addr = inet_addr( ip.c_str( ) ) ) == INADDR_NONE )
		return "Unknown";

	sin.sin_port = htons( 5555 ); //an arbitrary port since we're only interested in rdns
	char host[NI_MAXHOST], service[NI_MAXSERV];
	int s = getnameinfo( ( struct sockaddr * ) &sin, sizeof( sin ), host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV );
	string hostname( host );
	memset( &sin, 0, sizeof( sin ) );

	if( hostname.empty( ) )
		return "Unknown";

	HostNameInfo info;
	info.ip = ip;
	info.hostname = hostname;

	boost::mutex::scoped_lock lockInsert( m_HostNameCacheMutex );
	m_HostNameCache.push_back( info );
	while( m_HostNameCache.size( ) > 512 ) m_HostNameCache.pop_front( );
	lockInsert.unlock( );

	return info.hostname;
}
