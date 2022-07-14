/**
 * vim: set ts=4 :
 * =============================================================================
 * DoS Protect Extension
 * Copyright (C) 2009 raydan
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"

#if SH_SYS != SH_SYS_WIN32            \
    || (SOURCE_ENGINE != SE_LEFT4DEAD       \
    && SOURCE_ENGINE != SE_NUCLEARDAWN       \
    && SOURCE_ENGINE != SE_LEFT4DEAD2       \
    && SOURCE_ENGINE != SE_CSGO)
#include "CDetour/detours.h"
#endif

#if SH_SYS == SH_SYS_WIN32            \
    && (SOURCE_ENGINE == SE_LEFT4DEAD       \
    || SOURCE_ENGINE == SE_NUCLEARDAWN       \
    || SOURCE_ENGINE == SE_LEFT4DEAD2       \
    || SOURCE_ENGINE == SE_CSGO)
SH_DECL_MANUALHOOK6(Hook_RecvFrom, 0, 0, 0, int, int, char *, int, int, sockaddr *, int *)
#endif

DoSProtect g_DoSProtect;
SMEXT_LINK(&g_DoSProtect);

ICvar *icvar = nullptr;

#if SH_SYS == SH_SYS_WIN32            \
    && (SOURCE_ENGINE == SE_LEFT4DEAD       \
    || SOURCE_ENGINE == SE_NUCLEARDAWN       \
    || SOURCE_ENGINE == SE_LEFT4DEAD2       \
    || SOURCE_ENGINE == SE_CSGO)
void *g_pSteamSocketMgr = nullptr;
#else
CDetour *g_RecvFromDetour = nullptr;
#endif

SourceHook::List<DoS_Attacks *> g_DoS_Attacks_List;

void OnAttacksCmd()
{
    SourceHook::List<DoS_Attacks *>::iterator iter = g_DoS_Attacks_List.begin();

    DoS_Attacks *atkr;
    char szTime[32];

    Msg("List of recorded DoS attacks:\n");
    Msg("================================================================================\n");

    while (iter != g_DoS_Attacks_List.end())
    {
        atkr = (*iter);

        strftime(szTime, sizeof(szTime), "%m/%d/%Y - %H:%M:%S", localtime(&atkr->time_val));

        Msg("> %s [%d packet(s) ; Last attempt: %s]\n", inet_ntoa(atkr->ipaddr4), atkr->count, szTime);

        iter++;
    }

    Msg("================================================================================\n");
}

ConCommand dosp_attacks("dosp_attacks", OnAttacksCmd, "", 0);

void SaveAttempt(in_addr ipaddr4)
{
    SourceHook::List<DoS_Attacks *>::iterator iter = g_DoS_Attacks_List.begin();

    DoS_Attacks *atkr;

    while (iter != g_DoS_Attacks_List.end())
    {
        atkr = (*iter);
        if (memcmp((void*)&ipaddr4, (void*)&atkr->ipaddr4, sizeof(in_addr)) == 0)
        {
            atkr->count++;
            atkr->time_val = time(nullptr);

            return;
        }

        iter++;
    }

    atkr = new DoS_Attacks;

    atkr->ipaddr4 = ipaddr4;
    atkr->count = 1;
    atkr->time_val = time(nullptr);

    g_DoS_Attacks_List.push_back(atkr);
}

#if SH_SYS != SH_SYS_WIN32            \
    || (SOURCE_ENGINE != SE_LEFT4DEAD       \
    && SOURCE_ENGINE != SE_NUCLEARDAWN       \
    && SOURCE_ENGINE != SE_LEFT4DEAD2       \
    && SOURCE_ENGINE != SE_CSGO)
DETOUR_DECL_STATIC6(Detour_RecvFrom, int, int, socket, char *, buffer, int, length, int, flags, sockaddr *, address, socklen_t *, address_len)
{
    int ret = DETOUR_STATIC_CALL(Detour_RecvFrom)(socket, buffer, length, flags, address, address_len);
    if (ret != 0)
    {
        return ret;
    }

    SaveAttempt(((sockaddr_in*)address)->sin_addr);
    return 25;
}
#else
int Hook_RecvFrom(int s, char *buf, int len, int flags, sockaddr *from, int *fromlen)
{
    int ret = recvfrom(s, buf, len, flags, from, fromlen);
    if (ret != 0)
    {
        RETURN_META_VALUE(MRES_SUPERCEDE, ret);
    }

    SaveAttempt(((sockaddr_in*)from)->sin_addr);
    RETURN_META_VALUE(MRES_SUPERCEDE, 25);
}
#endif

bool DoSProtect::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
#if SH_SYS != SH_SYS_WIN32            \
    || (SOURCE_ENGINE != SE_LEFT4DEAD       \
    && SOURCE_ENGINE != SE_NUCLEARDAWN       \
    && SOURCE_ENGINE != SE_LEFT4DEAD2       \
    && SOURCE_ENGINE != SE_CSGO)
    CDetourManager::Init(g_pSM->GetScriptingEngine(), nullptr);

    g_RecvFromDetour = DETOUR_CREATE_STATIC_PTR(Detour_RecvFrom, (gpointer)recvfrom);
    if (g_RecvFromDetour == nullptr)
    {
        ke::SafeStrcpy(error, maxlen, "Failed to setup detour for recvfrom.");
        return false;
    }
    
    g_RecvFromDetour->EnableDetour();

    rootconsole->ConsolePrint("[DoSP] Detouring recvfrom...");
#else
    IGameConfig *g_pGameConf = nullptr;

    char conf_error[255];
    if (!gameconfs->LoadGameConfigFile("dosprotect.games", &g_pGameConf, conf_error, sizeof(conf_error)))
    {
        if (error)
        {
            ke::SafeSprintf(error, maxlen, "Could not read dosprotect.games: %s", conf_error);
        }
        return false;
    }

    if (!g_pGameConf->GetAddress("SteamSocketMgr", &g_pSteamSocketMgr) || g_pSteamSocketMgr == nullptr)
    {
        ke::SafeStrcpy(error, maxlen, "Failed to find SteamSocketMgr pointer.");
        return false;
    }

    int offset;
    if (!g_pGameConf->GetOffset("CSteamSocketMgr::recvfrom", &offset))
    {
        ke::SafeStrcpy(error, maxlen, "Failed to get CSteamSocketMgr::recvfrom offset.");
        return false;
    }

    gameconfs->CloseGameConfigFile(g_pGameConf);

    SH_MANUALHOOK_RECONFIGURE(Hook_RecvFrom, offset, 0, 0);
    
    SH_ADD_MANUALHOOK(Hook_RecvFrom, g_pSteamSocketMgr, SH_STATIC(Hook_RecvFrom), false);

    rootconsole->ConsolePrint("[DoSP] Hooked CSteamSocketMgr::recvfrom successfully!");
#endif

#if SOURCE_ENGINE >= SE_ORANGEBOX
    g_pCVar = icvar;

    ConVar_Register(0, this);
#else
    ConCommandBaseMgr::OneTimeInit(this);
#endif
    return true;
}

void DoSProtect::SDK_OnUnload()
{
    SourceHook::List<DoS_Attacks *>::iterator iter = g_DoS_Attacks_List.end();
    while (iter != g_DoS_Attacks_List.begin())
    {
        delete (*iter);
        
        --iter;
    }

    g_DoS_Attacks_List.clear();

#if SH_SYS != SH_SYS_WIN32            \
    || (SOURCE_ENGINE != SE_LEFT4DEAD       \
    && SOURCE_ENGINE != SE_NUCLEARDAWN       \
    && SOURCE_ENGINE != SE_LEFT4DEAD2       \
    && SOURCE_ENGINE != SE_CSGO)
    if (g_RecvFromDetour)
    {
        g_RecvFromDetour->Destroy();
    }
#else
    SH_REMOVE_MANUALHOOK(Hook_RecvFrom, g_pSteamSocketMgr, SH_STATIC(Hook_RecvFrom), false);
#endif
}

bool DoSProtect::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
    GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
    return true;
}

bool DoSProtect::RegisterConCommandBase(ConCommandBase *pCommand)
{
    return META_REGCVAR(pCommand);
}