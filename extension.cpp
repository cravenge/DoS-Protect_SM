#include <sourcemod_version.h>
#include "extension.h"

#ifdef _WIN32
SH_DECL_MANUALHOOK6(Hook_RecvFrom, 0, 0, 0, int, int, char*, int, int, sockaddr*, int*)
#elif _LINUX
SH_DECL_MANUALHOOK6(Hook_RecvFrom, 0, 0, 0, int, int, char*, int, int, sockaddr*, socklen_t*)
#endif

void * g_pSteamSocketMgr = NULL;
IGameConfig *g_pGameConf = NULL;
ICvar * icvar = NULL;
ICvar * g_pCvar = NULL;

typedef struct Attacker{
	in_addr ip;
	int count;
	time_t time;
} Attacker;

#if defined _LINUX
	CDetour *g_pRecvFrom = 0;
#endif

SourceHook::List<Attacker *> g_Attackers;

ConCommand *g_Attacks = new ConCommand("dosp_attacks", Command_Attacks, "", 0);

DoSProtect g_DoSProtect;
SMEXT_LINK(&g_DoSProtect);

void Command_Attacks()
{
	Msg("DoSProtect logged attacks:\n");
	Msg(" Attacker IP\tPackets\tLast packet\n--------------------------------\n");
	SourceHook::List<Attacker *>::iterator iter;
	Attacker *atkr;
	char szTime[64];
	for(iter=g_Attackers.begin(); iter!=g_Attackers.end(); iter++)
	{
		atkr = (*iter);
		strftime(szTime, sizeof(szTime), "%Y.%m.%d %H:%M:%S", localtime(&atkr->time));
		Msg(" %s\t%d\t%s\n",inet_ntoa(atkr->ip),atkr->count,szTime);
	}
	Msg("--------------------------------\n");
}

void LogDoS(in_addr ip)
{
	SourceHook::List<Attacker *>::iterator iter;
	Attacker *atkr;
	for(iter=g_Attackers.begin(); iter!=g_Attackers.end(); iter++)
	{
		atkr = (*iter);
		if(memcmp((void*)&ip, (void*)&atkr->ip, sizeof(in_addr))==0)
		{
			++atkr->count;
			atkr->time=time(0);
			return;
		}
	}
	atkr = new Attacker;
	atkr->ip=ip;
	atkr->count=0;
	g_Attackers.push_back(atkr);
}

#if defined _WIN32
int Hook_RecvFrom(int s, char * buf, int len, int flags, sockaddr * from, int * fromlen)
{
	//Calling SteamSocketMgr::recvfrom resulted in fake and false reports
	//This seems to work just fine too
	int ret = recvfrom(s, buf, len, flags, from, fromlen);
	if(ret==0)
	{
		LogDoS(((sockaddr_in*)from)->sin_addr);
		RETURN_META_VALUE(MRES_SUPERCEDE, 25);
	}
	RETURN_META_VALUE(MRES_SUPERCEDE, ret);
}
#elif defined _LINUX
DETOUR_DECL_STATIC6(Detour_RecvFrom, int, int, s, char *, buf, int, len, int, flags, sockaddr *, from, socklen_t*, fromlen)
{
	int ret = DETOUR_STATIC_CALL(Detour_RecvFrom)(s, buf, len, flags, from, fromlen);
	if(ret==0)
	{
		LogDoS(((sockaddr_in*)from)->sin_addr);
		return 25;
	}
	return ret;
}
#endif

bool DoSProtect::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
#if defined _WIN32
	char conf_error[255];
	if(!gameconfs->LoadGameConfigFile("dosprotect", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		error = conf_error;
		return false;
	}

	g_pGameConf->GetAddress("SteamSocketMgr", &g_pSteamSocketMgr);
	if(!g_pSteamSocketMgr)
	{
		sprintf(error, "Failed to get address of SteamSocketMgr");
		return false;
	}

	int offset;
	if(!g_pGameConf->GetOffset("recvfrom", &offset))
	{
		sprintf(error, "Failed to get recvfrom offset");
		return false;
	}

	SH_MANUALHOOK_RECONFIGURE(Hook_RecvFrom, offset, 0, 0);
	SH_ADD_MANUALHOOK(Hook_RecvFrom, g_pSteamSocketMgr, SH_STATIC(Hook_RecvFrom), false);
#elif defined _LINUX
	CDetourManager::Init(g_pSM->GetScriptingEngine(), 0);
	g_pRecvFrom = DETOUR_CREATE_STATIC_PTR(Detour_RecvFrom, (gpointer)recvfrom);
	if (g_pRecvFrom == NULL)
	{
		snprintf(error, maxlength, "Failed to initialize the detour. Please contact the author.");
		return false;
	}
	g_pRecvFrom->EnableDetour();
#endif

	return true;
}

void DoSProtect::SDK_OnUnload()
{
#if defined _WIN32
	SH_REMOVE_MANUALHOOK(Hook_RecvFrom, g_pSteamSocketMgr, SH_STATIC(Hook_RecvFrom), false);
#elif defined _LINUX
	if(g_pRecvFrom)
		g_pRecvFrom->Destroy();
#endif

	SourceHook::List<Attacker *>::iterator iter;
	Attacker *atkr;
	for(iter=g_Attackers.begin(); iter!=g_Attackers.end(); iter++)
	{
		atkr = (*iter);
		delete atkr;
	}
	g_Attackers.clear();
	g_pCvar->UnregisterConCommand(g_Attacks);
}

void DoSProtect::SDK_OnAllLoaded()
{
}

bool DoSProtect::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	// I gave up figuring out the Metamod way after 10 minutes of trial and failure, this just worked
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCvar, ICvar, CVAR_INTERFACE_VERSION);
	g_pCvar->RegisterConCommand(g_Attacks);
	return true;
}