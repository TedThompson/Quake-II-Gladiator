
#include "g_local.h"

#ifdef LOGFILE
#include "g_log.h"
#endif //LOGFILE

#ifdef BOT
#include "bl_main.h"
#include "bl_spawn.h"
#endif //BOT

game_locals_t   game;
level_locals_t  level;
game_import_t   gi;
game_export_t   globals;
spawn_temp_t    st;

int sm_meat_index;
int snd_fry;
int meansOfDeath;

#ifdef BOT
int paused;
cvar_t* sp_dm;
cvar_t* sp_dm_tier;
#endif //BOT

edict_t* g_edicts;

cvar_t* deathmatch;
cvar_t* coop;
cvar_t* dmflags;
cvar_t* skill;
cvar_t* fraglimit;
cvar_t* timelimit;
cvar_t* password;
cvar_t* spectator_password;
cvar_t* needpass;
cvar_t* maxclients;
cvar_t* maxspectators;
cvar_t* maxentities;
cvar_t* g_select_empty;
cvar_t* dedicated;

cvar_t* filterban;

cvar_t* sv_maxvelocity;
cvar_t* sv_gravity;

cvar_t* sv_rollspeed;
cvar_t* sv_rollangle;
cvar_t* gun_x;
cvar_t* gun_y;
cvar_t* gun_z;

cvar_t* run_pitch;
cvar_t* run_roll;
cvar_t* bob_up;
cvar_t* bob_pitch;
cvar_t* bob_roll;

cvar_t* sv_cheats;

cvar_t* flood_msgs;
cvar_t* flood_persecond;
cvar_t* flood_waitdelay;

cvar_t* sv_maplist;

void SpawnEntities(char* mapname, char* entities, char* spawnpoint);
void ClientThink(edict_t* ent, usercmd_t* cmd);
qboolean ClientConnect(edict_t* ent, char* userinfo);
void ClientUserinfoChanged(edict_t* ent, char* userinfo);
void ClientDisconnect(edict_t* ent);
void ClientBegin(edict_t* ent);
void ClientCommand(edict_t* ent);
//void RunEntity (edict_t *ent); From original code, goes nowhere, does nothing??
void WriteGame(char* filename, qboolean autosave);
void ReadGame(char* filename);
void WriteLevel(char* filename);
void ReadLevel(char* filename);
void InitGame(void);
void G_RunFrame(void);


//===================================================================

void ShutdownGame(void)
{
#ifdef BOT
    BotUnloadAllLibraries();
#endif
#ifdef LOGFILE
    Log_ShutDown();
#endif //LOGFILE
    gi.dprintf("==== ShutdownGame ====\n");

    gi.FreeTags(TAG_LEVEL);
    gi.FreeTags(TAG_GAME);
}


/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
Q2_DLL_EXPORTED game_export_t* GetGameAPI(game_import_t* import)
{
    gi = *import;

#ifdef BOT_IMPORT
    BotRedirectGameImport();
#endif //BOT_IMPORT

#ifdef BOT
    Swap_Init();
#endif //BOT

    globals.apiversion = GAME_API_VERSION;
    globals.Init = InitGame;
    globals.Shutdown = ShutdownGame;
    globals.SpawnEntities = SpawnEntities;

    globals.WriteGame = WriteGame;
    globals.ReadGame = ReadGame;
    globals.WriteLevel = WriteLevel;
    globals.ReadLevel = ReadLevel;

    globals.ClientThink = ClientThink;
    globals.ClientConnect = ClientConnect;
    globals.ClientUserinfoChanged = ClientUserinfoChanged;
    globals.ClientDisconnect = ClientDisconnect;
    globals.ClientBegin = ClientBegin;
    globals.ClientCommand = ClientCommand;

    globals.RunFrame = G_RunFrame;

    globals.ServerCommand = ServerCommand;

    globals.edict_size = sizeof(edict_t);

    return &globals;
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error(char* error, ...)
{
    va_list     argptr;
    char        text[1024];

    va_start(argptr, error);
    vsprintf(text, error, argptr);
    va_end(argptr);

    gi.error(ERR_FATAL, "%s", text);
}

void Com_Printf(char* msg, ...)
{
    va_list     argptr;
    char        text[1024];

    va_start(argptr, msg);
    vsprintf(text, msg, argptr);
    va_end(argptr);

    gi.dprintf("%s", text);
}

#endif

//======================================================================


/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames(void)
{
    int     i;
    edict_t* ent;

    // calc the player views now that all pushing
    // and damage has been added
    for (i = 0; i < maxclients->value; i++)
    {
        ent = g_edicts + 1 + i;
        if (!ent->inuse || !ent->client)
            continue;
        ClientEndServerFrame(ent);
    }

}

/*
=================
CreateTargetChangeLevel

Returns the created target changelevel
=================
*/
edict_t* CreateTargetChangeLevel(char* map)
{
    edict_t* ent;

    ent = G_Spawn();
    ent->classname = "target_changelevel";
    Com_sprintf(level.nextmap, sizeof(level.nextmap), "%s", map);
    ent->map = level.nextmap;
    return ent;
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel(void)
{
    edict_t* ent;
    char* s, * t, * f;
    char cinematic[64], bsp[64];
    static const char* seps = " ,\n\r";
#ifdef BOT
    qboolean sp_winner = false;
    int i, j, highscore = -1, hubs_index[4] = { 0 };
    cvar_t* tier;

    if (sp_dm->value)
    {
        //see if a human had the highest score
        for (i = 0; i < game.maxclients; i++)
        {
            ent = g_edicts + 1 + i;

            if (!ent->inuse || game.clients[i].resp.spectator)
                continue;

            if (game.clients[i].resp.score > highscore)
            {
                highscore = game.clients[i].resp.score;

                if (ent->flags & FL_BOT)
                    sp_winner = false;
                else
                    sp_winner = true;
            }
            else if (game.clients[i].resp.score == highscore)
            {
                sp_winner = false;
            }
        }
    }
#endif //BOT
    // stay on same level flag
    if (((int)dmflags->value & DF_SAME_LEVEL)
#ifdef BOT
        || (sp_dm->value && !sp_winner)
#endif //BOT
        )
    {
        BeginIntermission(CreateTargetChangeLevel(level.mapname));
        return;
    }
#ifdef BOT //Look for hub levels in sv_maplist to determine end of Tier.
    if (sp_dm->value && *sv_maplist->string)
    {
        s = strdup(sv_maplist->string);
        f = NULL;
        t = strtok(s, seps);
        i = j = 0;
        while (t != NULL)
        {
            if (Q_stricmp(t, "hub") == 0)
            {
                hubs_index[j] = i;
                j++;
            }
            t = strtok(NULL, seps);
            i++;
        }
        free(s);
    }
#endif
    // see if it's in the map list
    if (*sv_maplist->string)
    {
        s = strdup(sv_maplist->string);
        f = NULL;
        t = strtok(s, seps);
#ifdef BOT
        i = 1;
        tier = gi.cvar("sp_dm_tier", "", 0);
#endif
        while (t != NULL)
        {
            // check for *.cin+mapname and throw out *.cin+ for mapname matching
            if (sscanf(t, "%[^+]%*c%s", cinematic, bsp) != 2)
            {
                strcpy(bsp, t);
            }

            if (Q_stricmp(bsp, level.mapname) == 0)
            {
#ifdef BOT
                if (i == hubs_index[0])
                    if (tier->value < 1)
                        gi.cvar_forceset("sp_dm_tier", va("%d", (int)1));
                if (i == hubs_index[1])
                    if (tier->value < 2)
                        gi.cvar_forceset("sp_dm_tier", va("%d", (int)2));
                if (i == hubs_index[2])
                    if (tier->value < 3)
                        gi.cvar_forceset("sp_dm_tier", va("%d", (int)3));
#endif
                // it's in the list, go to the next one
                t = strtok(NULL, seps);
                if (t == NULL) // end of list, go to first one 
                {
                    if (f == NULL) // there isn't a first one, same level
                        BeginIntermission(CreateTargetChangeLevel(level.mapname));
                    else
                        BeginIntermission(CreateTargetChangeLevel(f));
                }
                else
                    BeginIntermission(CreateTargetChangeLevel(t));
                free(s);
                return;
            }
            if (!f)
                f = t;
            t = strtok(NULL, seps);
#ifdef BOT
            i++;
#endif
        }
        free(s);
    }

    if (level.nextmap[0]) // go to a specific map
        BeginIntermission(CreateTargetChangeLevel(level.nextmap));
    else {  // search for a changelevel
        ent = G_Find(NULL, FOFS(classname), "target_changelevel");
        if (!ent)
        {   // the map designer didn't include a changelevel,
            // so create a fake ent that goes back to the same level
            BeginIntermission(CreateTargetChangeLevel(level.mapname));
            return;
        }
        BeginIntermission(ent);
    }
}


/*
=================
CheckNeedPass
=================
*/
void CheckNeedPass(void)
{
    int need;

    // if password or spectator_password has changed, update needpass
    // as needed
    if (password->modified || spectator_password->modified)
    {
        password->modified = spectator_password->modified = false;

        need = 0;

        if (*password->string && Q_stricmp(password->string, "none"))
            need |= 1;
        if (*spectator_password->string && Q_stricmp(spectator_password->string, "none"))
            need |= 2;

        gi.cvar_set("needpass", va("%d", need));
    }
}

/*
=================
CheckDMRules
=================
*/
void CheckDMRules(void)
{
    int         i;
    gclient_t* cl;

    if (level.intermissiontime)
        return;

    if (!deathmatch->value)
        return;

    if (timelimit->value)
    {
        if (level.time >= timelimit->value * 60)
        {
            gi.bprintf(PRINT_HIGH, "Timelimit hit.\n");
            EndDMLevel();
            return;
        }
    }

    if (fraglimit->value)
    {
        for (i = 0; i < maxclients->value; i++)
        {
            cl = game.clients + i;
            if (!g_edicts[i + 1].inuse)
                continue;

            if (cl->resp.score >= fraglimit->value)
            {
                gi.bprintf(PRINT_HIGH, "Fraglimit hit.\n");
                EndDMLevel();
                return;
            }
        }
    }
}


/*
=============
ExitLevel
=============
*/
void ExitLevel(void)
{
    int     i;
    edict_t* ent;
    char    command[256];

    Com_sprintf(command, sizeof(command), "gamemap \"%s\"\n", level.changemap);
    gi.AddCommandString(command);
    level.changemap = NULL;
    level.exitintermission = 0;
    level.intermissiontime = 0;
    ClientEndServerFrames();

    // clear some things before going to next level
    for (i = 0; i < maxclients->value; i++)
    {
        ent = g_edicts + 1 + i;
        if (!ent->inuse)
            continue;
        if (ent->health > ent->client->pers.max_health)
            ent->health = ent->client->pers.max_health;
#ifdef BOT
        if (deathmatch->value)
            ent->client->resp.score = ent->client->pers.score = 0;
#endif //BOT
    }
}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
void G_RunFrame(void)
{
    int     i;
    edict_t* ent;

#ifdef BOT
    if (paused) return;
#endif //BOT

    level.framenum++;
    level.time = level.framenum * FRAMETIME;

    // choose a client for monsters to target this frame
    AI_SetSightClient();

    // exit intermissions

    if (level.exitintermission)
    {
        ExitLevel();
        return;
    }

#ifdef BOT
    //
    AddQueuedBots();
    //start the bot library frame
    BotLib_BotStartFrame(level.time);
#endif //BOT

    //
    // treat each object in turn
    // even the world gets a chance to think
    //
    ent = &g_edicts[0];
    for (i = 0; i < globals.num_edicts; i++, ent++)
    {
        if (!ent->inuse)
            continue;

        level.current_entity = ent;

#ifdef BOT
        //used for laser lines
        if (!(ent->flags & FL_OLDORGNOTSET))
#endif //BOT
            VectorCopy(ent->s.origin, ent->s.old_origin);

        // if the ground entity moved, make sure we are still on it
        if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount))
        {
            ent->groundentity = NULL;
            if (!(ent->flags & (FL_SWIM | FL_FLY)) && (ent->svflags & SVF_MONSTER))
            {
                M_CheckGround(ent);
            }
        }

        if (i > 0 && i <= maxclients->value)
        {
            ClientBeginServerFrame(ent);
            continue;
        }
        G_RunEntity(ent);
    }

#ifdef BOT
    ent = &g_edicts[0];
    for (i = 0; i < globals.num_edicts; i++, ent++)
    {
        if (!ent->inuse) continue;
        if (!(ent->svflags & SVF_NOCLIENT))
        {
            BotLib_BotUpdateEntity(ent);
        } //end if
    } //end for
    //The bot AI and execution of the input is prefered
    //not to be called between entity, sound and client
    //updates. This way the bot won't miss any updates.
    //It won't hurt to call them between the updates,
    //it's just that the bot might miss updates and
    //perform less good as a result.
    //So we just call it after the entities have been updated.
    //if ((level.framenum & 7) == 7)
    for (i = 0; i < maxclients->value; i++)
    {
        ent = DF_CLIENTENT(i);
        if (ent->inuse)
        {
            if (ent->flags & FL_BOT)
            {
                if (BotStarted(ent))
                {
                    //gi.dprintf("%6d bot AI\n", level.framenum);
                    //update the bot client state
                    BotLib_BotUpdateClient(ent);
                    //activate the bot AI for the current client for one server frame
                    BotLib_BotAI(ent, FRAMETIME);
                    //execute the bot input
                    BotExecuteInput(ent);
                } //end if
            } //end if
        } //end if
    } //end for
    //check if a minimum number of players should be in the game
    CheckMinimumPlayers();
#endif //BOT

    // see if it is time to end a deathmatch
    CheckDMRules();

    // see if needpass needs updated
    CheckNeedPass();

    // build the playerstate_t structures for all players
    ClientEndServerFrames();

#ifdef CH
    if (ch->value)
    {
        UpdateColoredHitman();
    } //end if
#endif //CH
}