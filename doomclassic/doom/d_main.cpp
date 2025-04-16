/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "Precompiled.h"
#include "globaldata.h"

#include "idlib/precompiled.h"
#ifdef USE_OPENXR
#include "renderer/OpenXR/XRCommon.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "doomdef.h"
#include "doomstat.h"

#include "dstrings.h"
#include "sounds.h"


#include "z_zone.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"

#include "f_finale.h"
#include "f_wipe.h"

#include "m_argv.h"
#include "m_misc.h"
#include "m_menu.h"

#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"

#include "g_game.h"

#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"

#include "p_setup.h"
#include "r_local.h"


#include "d_main.h"

//#include "precompiled.h"
//#include "../Main/PlayerProfile.h"
//#include "../Main/PSN/PS3_Session.h"
#include "d3xp/Game_local.h"
#include "framework/FileSystem.h"
#include "d_exp.h"
#include "d_deh.h"
//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
void D_DoomLoop (void);

void R_ExecuteSetViewSize (void);
void D_CheckNetGame (void);
bool D_PollNetworkStart();
void D_ProcessEvents (void);
void D_DoAdvanceDemo (void);

bool initonce = false;

idCVar cl_expMenu("cl_expMenu", "1", CVAR_INTEGER, "Change DOOM 2 Menu based on expansion 1 = DOOM 2, 2 = TNT: Evilution, 3 = The Plutonia Experiment, 4 = Master Levels, 5 = No Rest For the Living", 1, 5);
idCVar cl_ScreenSize("cl_ScreenSize", "0", CVAR_INTEGER | CVAR_BOOL, "Store ScreenSize input", 0, 1);


const char*		wadfiles[MAXWADFILES] =
{
	0
};

const char*		extraWad = 0;

//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//


//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent (event_t* ev)
{
	::g->events[::g->eventhead] = *ev;
	::g->eventhead = (++::g->eventhead)&(MAXEVENTS-1);
}


//
// D_ProcessEvents
// Send all the ::g->events of the given timestamp down the responder chain
//
void D_ProcessEvents (void)
{
	event_t*	ev;

	// IF STORE DEMO, DO NOT ACCEPT INPUT
	if ( ( ::g->gamemode == commercial )
		&& (W_CheckNumForName("map01")<0) )
		return;

	for ( ; ::g->eventtail != ::g->eventhead ; ::g->eventtail = (++::g->eventtail)&(MAXEVENTS-1) )
	{
		ev = &::g->events[::g->eventtail];
		if (::g->captureBind) {
			if (ev->type == ev_keydown) {
				M_Remap(ev);
			}
			continue;
		}
		if (M_Responder (ev))
			continue;               // menu ate the event
		G_Responder (ev);
	}
}




//
// D_Display
//  draw current display, possibly wiping it from the previous
//
// ::g->wipegamestate can be set to -1 to force a ::g->wipe on the next draw
extern bool waitingForWipe;

void D_Wipe()
{
	int nowtime, tics;

	nowtime = I_GetTime();
	tics = nowtime - ::g->wipestart;

	if (tics != 0)
	{
		::g->wipestart = nowtime;
		::g->wipedone = wipe_ScreenWipe( 0, 0, ::g->SCREENWIDTH, SCREENHEIGHT, tics );

		// DHM - Nerve :: Demo recording :: Stop large hitch on first frame after the wipe
		if ( ::g->wipedone ) {
			::g->oldtrt_entertics = nowtime / ::g->ticdup;
			::g->gametime = nowtime;
			::g->wipe = false;
			waitingForWipe = false;
		}
	}
}


void D_Display (void)
{
	qboolean			redrawsbar;

	if (::g->nodrawers)
		return;                    // for comparative timing / profiling

	redrawsbar = false;
	//int scaler = GLOBAL_IMAGE_SCALER - (::g->ASPECT_IMAGE_SCALER-GLOBAL_IMAGE_SCALER);//GK: Calculate image scaling based on aspect ratio
	// change the view size if needed
	if (::g->setsizeneeded)
	{
		R_ExecuteSetViewSize();
		::g->oldgamestate = (gamestate_t)-1;                      // force background redraw
		::g->borderdrawcount = 3;
	}

	// save the current screen if about to ::g->wipe
	if (::g->gamestate != ::g->wipegamestate)
	{
		::g->wipe = true;
		wipe_StartScreen(0, 0, ::g->SCREENWIDTH, SCREENHEIGHT);
	}
	else
		::g->wipe = false;

	if ((::g->gamestate == GS_LEVEL || ::g->gamestate == GS_DEMOLEVEL) && ::g->gametic)
		HU_Erase();

	

	// draw buffered stuff to screen
	I_UpdateNoBlit ();
#ifdef USE_OPENXR
	if (renderSystem->GetStereo3DMode() == STEREO3D_VR) {
		if (::g->gamestate == GS_LEVEL) {
			xrSystem->SetActionSet("GAME");
		}
		else {
			xrSystem->SetActionSet("MENU");
		}
	}
#endif
	// draw the view directly
	if ((::g->gamestate == GS_LEVEL || ::g->gamestate == GS_DEMOLEVEL) && !::g->automapactive && ::g->gametic)
		R_RenderPlayerView (&::g->players[::g->displayplayer]);

	// do buffered drawing
	switch (::g->gamestate)
	{
	case GS_DEMOLEVEL:
	case GS_LEVEL:
		if (!::g->gametic)
			break;
		if (::g->automapactive)
			AM_Drawer();

		if (::g->wipe || (::g->viewheight != 200 * GLOBAL_IMAGE_SCALER && ::g->fullscreen))
			redrawsbar = true;
		if (::g->inhelpscreensstate && !::g->inhelpscreens)
			redrawsbar = true;              // just put away the help screen
		ST_Drawer(::g->viewheight == 200 * GLOBAL_IMAGE_SCALER, redrawsbar);
		::g->fullscreen = ::g->viewheight == 200 * GLOBAL_IMAGE_SCALER;
		break;

	case GS_INTERMISSION:
		WI_Drawer();
		break;

	case GS_FINALE:
		F_Drawer();
		break;

	case GS_DEMOSCREEN:
		D_PageDrawer();
		break;
	}

	if ((::g->gamestate == GS_LEVEL || ::g->gamestate == GS_DEMOLEVEL) && ::g->gametic)
		HU_Drawer();
	// clean up border stuff
	if (::g->gamestate != ::g->oldgamestate && (::g->gamestate != GS_LEVEL && ::g->gamestate != GS_DEMOLEVEL))
		I_SetPalette ((byte*)W_CacheLumpName ("PLAYPAL",PU_CACHE_SHARED), W_LumpLength(W_GetNumForName("PLAYPAL")));

	// see if the border needs to be initially drawn
	if ((::g->gamestate == GS_LEVEL || ::g->gamestate == GS_DEMOLEVEL) && (::g->oldgamestate != GS_LEVEL && ::g->oldgamestate != GS_DEMOLEVEL))
	{
		::g->viewactivestate = false;        // view was not active
		R_FillBackScreen ();    // draw the pattern into the back screen
	}

	// see if the border needs to be updated to the screen
	if ((::g->gamestate == GS_LEVEL || ::g->gamestate == GS_DEMOLEVEL) && !::g->automapactive && ::g->scaledviewwidth != (320 * GLOBAL_IMAGE_SCALER) )
	{
		if (::g->menuactive || ::g->menuactivestate || !::g->viewactivestate)
			::g->borderdrawcount = 3;
		if (::g->borderdrawcount)
		{
			R_DrawViewBorder ();    // erase old menu stuff
			::g->borderdrawcount--;
		}

	}

	::g->menuactivestate = ::g->menuactive;
	::g->viewactivestate = ::g->viewactive;
	::g->inhelpscreensstate = ::g->inhelpscreens;
	::g->oldgamestate = ::g->wipegamestate = ::g->gamestate;

	// draw pause pic
	//GK: Just Draw it already
	int y = 0;
	if (::g->paused)
	{
		if (::g->automapactive)
			y = 4;
		else
			y = ::g->viewwindowy+4;
		V_DrawPatchDirect(::g->viewwindowx+(ORIGINAL_WIDTH-68)/2,
			y,0,img2lmp((patch_t*)W_CacheLumpName ("M_PAUSE", PU_CACHE_SHARED),W_GetNumForName("M_PAUSE")), false);
	}


	// menus go directly to the screen
	M_Drawer ();          // menu is drawn even on top of everything
	NetUpdate ( NULL );         // send out any new accumulation
	
	// normal update
	if (!::g->wipe)
	{
		I_FinishUpdate ();              // page flip or blit buffer
		return;
	}

	// \ update
	wipe_EndScreen(0, 0, ::g->SCREENWIDTH, SCREENHEIGHT);

	::g->wipestart = I_GetTime () - 1;

	D_Wipe(); // initialize g->wipedone
}



void D_RunFrame( bool Sounds )
{
	if (Sounds)	{
		// move positional sounds
		S_UpdateSounds (::g->players[::g->consoleplayer].mo);
	}
	if (!::g->demoplayback) {
		M_SizeDisplay(cl_ScreenSize.GetInteger());
	}
	// Update display, next frame, with current state.
	D_Display ();

	if (Sounds)	{
		// Update sound output.
		I_SubmitSound();
	}
}



//
//  D_DoomLoop
//
void D_DoomLoop (void)
{
	// DHM - Not used
/*
	if (M_CheckParm ("-debugfile"))
	{
		char    filename[20];
		sprintf (filename,"debug%i.txt",::g->consoleplayer);
		I_Printf ("debug output to: %s\n",filename);
		::g->debugfile = f o p e n(filename,"w");
	}

	I_InitGraphics ();

	while (1)
	{
		TryRunTics();
		D_RunFrame( true );
	}
*/
}



//
//  DEMO LOOP
//


//
// D_PageTicker
// Handles timing for warped ::g->projection
//
void D_PageTicker (void)
{
	if (--::g->pagetic < 0)
		D_AdvanceDemo ();
}



//
// D_PageDrawer
//
void D_PageDrawer (void)
{
	//GK: Wide TITLEPIC image supprt
	patch_t* image = img2lmp(W_CacheLumpName(::g->pagename, PU_CACHE_SHARED), W_GetNumForName(::g->pagename));
	int offsets = image->width > ORIGINAL_WIDTH ? abs(image->width - ::g->renderingWidth) / 2 : 0;
	V_DrawPatch (0,0, 0, /*(patch_t*)*/image, image->width > ORIGINAL_WIDTH, offsets, offsets);
}


//
// D_AdvanceDemo
// Called after each demo or intro ::g->demosequence finishes
//
void D_AdvanceDemo (void)
{
	::g->advancedemo = true;
}


//
// This cycles through the demo sequences.
// FIXME - version dependend demo numbers?
//
void D_DoAdvanceDemo (void)
{

	if (::g->paused)
		return;

	::g->players[::g->consoleplayer].playerstate = PST_LIVE;  // not reborn
	::g->advancedemo = false;
	::g->usergame = false;               // no save / end game here
	::g->paused = false;
	::g->gameaction = ga_nothing;
	M_SizeDisplay(0);
	if ( ::g->gamemode == retail )
		::g->demosequence = (::g->demosequence+1)%8;
	else
		::g->demosequence = (::g->demosequence+1)%6;

	//GK: in case of Master Levels and No Rest for the Living there are no demos so keep looping the music
	if (::g->gamemission == pack_master || ::g->gamemission == pack_nerve) {
		::g->demosequence = 0;
	}
	//GK: Change "INTERPIC" with "TITLEPIC"
	switch (::g->demosequence)
	{
	case 0:
		if ( ::g->gamemode == commercial )
			::g->pagetic = 11 * TICRATE;
		else
			::g->pagetic = 8 * TICRATE;

		::g->gamestate = GS_DEMOSCREEN;
		//GK change INTERPIC with TITLEPIC
		::g->pagename = (char*)"TITLEPIC";

		if (::g->gamemode == commercial)
		S_ChangeMusic(mus_dm2ttl, true);
		else
		S_ChangeMusic(mus_intro, true);

		break;
	case 1:
		G_DeferedPlayDemo("demo1");
		break;
	case 2:
		::g->pagetic = 3 * TICRATE;
		::g->gamestate = GS_DEMOSCREEN;
		::g->pagename = (char*)"TITLEPIC";
		break;
	case 3:
		G_DeferedPlayDemo("demo2");
		break;
	case 4:
		::g->pagetic = 3 * TICRATE;
		::g->gamestate = GS_DEMOSCREEN;
		::g->pagename = (char*)"TITLEPIC";
		break;
	case 5:
		G_DeferedPlayDemo("demo3");
		break;
		// THE DEFINITIVE DOOM Special Edition demo
	case 6:
		::g->pagetic = 3 * TICRATE;
		::g->gamestate = GS_DEMOSCREEN;
		::g->pagename = (char*)"TITLEPIC";
		break;
	case 7:
		G_DeferedPlayDemo("demo4");
		break;
	}
}



//
// D_StartTitle
//
void D_StartTitle(void)
{
	::g->gameaction = ga_nothing;
	::g->demosequence = -1;
	D_AdvanceDemo();
}




//      print ::g->title for every printed line

//
// D_AddExtraWadFile
//
void D_SetExtraWadFile(const char* file) {
	extraWad = file;
}

//
// D_AddFile
//
void D_AddFile(const char* file)
{
	int     numwadfiles;

	for (numwadfiles = 0; wadfiles[numwadfiles]; numwadfiles++)
		if (file == wadfiles[numwadfiles])
			return;
	;
	if (W_IsGameWAD(file))
		numwadfiles = 0;
	wadfiles[numwadfiles] = file;
}

//
// IdentifyVersion
// Checks availability of IWAD files by name,
// to determine whether registered/commercial features
// should be executed (notably loading PWAD's).
//

void IdentifyVersion(void)
{
	W_FreeWadFiles();


	const ExpansionData* expansion = DoomLib::GetCurrentExpansion();
	::g->gamemode = expansion->gameMode;
	::g->gamemission = expansion->pack_type;
	::g->rexp = expansion->pack_type;

	if (expansion->type == ExpansionData::PWAD) {
		D_AddFile(expansion->iWadFilename);
		for(int i = 0; i < expansion->pWadFilenames.size(); i++) {
			D_AddFile(expansion->pWadFilenames[i].c_str());
		}

	}
	else {
		if (::g->gamemode == retail) {
			if (fileSystem->OpenFileRead(expansion->iWadFilename) == 0){
			const ExpansionData* expansiondemo = DoomLib::GetDemo();
			::g->gamemode = expansiondemo->gameMode;
			::g->gamemission = expansiondemo->pack_type;
			::g->rexp = expansiondemo->pack_type;
			D_AddFile(expansiondemo->iWadFilename);
			}
			else {
				D_AddFile(expansion->iWadFilename);
			}
		}
		else {
			D_AddFile(expansion->iWadFilename);
		}
		
	}
	
}


//
// Find a Response File
//
void FindResponseFile (void)
{
}

//GK: Begin
//
//	CalculateInterpolation
// Calculate the double logic frame frequency in case 
// the denominator Hz is higher than the latched Hz.
// 
// For 60 FPS it ensures that in 1 second it will
// successfilly run 35-40 logical frames.
//
// The first frequency is based on the half of the denominator Hz
// and it is usually the higher of them both.
// The second is based on the remaining frames from the first
// frequency and is the lower one.
//void CalculateInterpolation() {
//	int engineHz_denominator = com_engineHz_denominator / 100LL;
//	if (engineHz_denominator > com_engineHz_latched) {
//		double firstclassicrate = engineHz_denominator;
//		while (firstclassicrate > com_engineHz_latched) {
//			firstclassicrate = firstclassicrate / 2;
//		}
//		::g->ticrate[0] = ceil(engineHz_denominator / firstclassicrate);
//
//		int ff = ceil(engineHz_denominator / ((double)::g->ticrate[0]));
//
//		double remainedticrate = com_engineHz_latched - ff;
//		for (int i = 1; i < 3; i++) {
//			::g->ticrate[i] = ceil(engineHz_denominator / remainedticrate);
//			int af = ceil(engineHz_denominator / ((double)::g->ticrate[i]));
//			if (remainedticrate - af == 0) {
//				break;
//			}
//			remainedticrate = abs(remainedticrate - af);
//		}
//	}
//}
//GK: End

//
// D_DoomMain
//

void D_DoomMain(void)
{
	int             p;
	char                    file[256];
	R_Initwidth(); //GK: Simplyfied
	FindResponseFile();
	//CalculateInterpolation();


	IdentifyVersion();
	::g->modifiedtext = false;
	::g->modftext = false;
	::g->modind = 0;
	if (::g->gamemode == retail) { //GK:Dealing with an issue about moded save files
		DoomLib::SetIdealExpansion(doom);
	}
	//GK: New pwad for compatibility with Evilution and Plutonia (only for DOOM II)
	if (::g->gamemode == commercial) {
		D_AddFile("wads/ua.wad");
	}
	// if (::g->gamemission == pack_master) {
	// 	D_AddFile("wads/mlbls.wad");
	// }
	//GK: New pwad for compatibility with the original DOOM And DOOMII IWADs
	D_AddFile("wads/newopt.wad");

	//GK: Check if there is either a folder or a zip that is called "master" and create the MASTERLEVELS.wad
	if (::g->gamemode == commercial && !DoomLib::hexp[2])
		MakeMaster_Wad();
	//GK: Find the position of -doom,-doom2 and -both
	M_initParam();
	//GK: fix for Dehacked pointer editor
	init_info();
	initWeapons();
	initMapNames();
	initModdableTexts();
	setbuf(stdout, NULL);
	::g->modifiedgame = false;

	// TODO: Networking GK:NOT NEEDED
	//const bool isDeathmatch = session->GetActingGameStateLobbyBase().GetMatchParms().gameMode == GAME_MODE_PVP;
	//const bool isDeathmatch = false;
	//GK begin
	::g->classichUsed = false;
	::g->warpUsed = false;
	//GK: Check here for deathmatch
	if (::g->gamemode == retail || (::g->gamemode == commercial && !initonce)) {
		::g->devparm = M_CheckParm("-devparm");
		if (M_CheckParm("-altdeath"))
			::g->deathmatch = 2;
		else if (M_CheckParm("-deathmatch"))
			::g->deathmatch = 1;
	}
	::g->classiccheats = M_CheckParm("-classich");
	::g->classichUsed = ::g->classiccheats;
	//GK End
	if (::g->gamemode == retail || (::g->gamemode == commercial && !initonce)) {
		::g->nomonsters = M_CheckParm("-nomonsters") || ::g->deathmatch;
		::g->respawnparm = M_CheckParm("-respawn");
	}
	::g->fastparm = M_CheckParm("-fast");
	
	switch (::g->gamemode)
	{
	case retail:
		sprintf(::g->title,
			"The Ultimate DOOM v%i.%i",
			VERSION / 100, VERSION % 100);
		break;
	case shareware:
		sprintf(::g->title,
			"DOOM Shareware v%i.%i",
			VERSION / 100, VERSION % 100);
		break;
	case registered:
		sprintf(::g->title,
			"DOOM Registered v%i.%i",
			VERSION / 100, VERSION % 100);
		break;
	case commercial:
		sprintf(::g->title,
			"DOOM 2: Hell on Earth v%i.%i",
			VERSION / 100, VERSION % 100);
		break;
	default:
		sprintf(::g->title,
			"Public DOOM - v%i.%i",
			VERSION / 100, VERSION % 100);
		break;
	}

	if (::g->devparm)
		I_Printf(D_DEVSTR);
	if (::g->gamemode == retail || (::g->gamemode == commercial && !initonce)) {
		if (M_CheckParm("-cdrom"))
		{
			I_Printf(D_CDROM);
			//c++		mkdir("c:\\doomdata",0);
			strcpy(::g->basedefault, "c:/doomdata/default.cfg");
		}
	}
	// add any files specified on the command line with -file ::g->wadfile
	// to the wad list
	//
	// get skill / episode / map from defaults
#ifdef FOOLS
	::g->startskill = sk_masochism;
#else
	::g->startskill = sk_medium;
#endif
	::g->startepisode = 1;
	::g->startmap = 1;
	::g->autostart = false;
	if (::g->gamemode == commercial && !initonce) {
		p = M_CheckParm("-exp");
		if (p)
		{
			::g->exp = ::g->myargv[p + 1][0] - '0';
			switch (::g->exp) {
			case 1:
				DoomLib::SetIdealExpansion(doom2);
				break;
			case 2:
				DoomLib::SetIdealExpansion(pack_nerve);
				break;
			case 3:
				DoomLib::SetIdealExpansion(pack_tnt);
				break;
			case 4:
				DoomLib::SetIdealExpansion(pack_plut);
				break;
			case 5:
				DoomLib::SetIdealExpansion(pack_master);
				break;
			default:
				DoomLib::SetIdealExpansion(doom2);
				break;
			}
			int expInd = DoomLib::idealExpansion - 2;
			if (!DoomLib::hexp[expInd]) {
				DoomLib::SetIdealExpansion(doom2);
			}
			DoomLib::SetCurrentExpansion(DoomLib::idealExpansion);
			idLib::Printf("Loading %d\n", DoomLib::idealExpansion);
			idLib::Printf("Loading %d\n", DoomLib::expansionSelected);
			::g->autostart = true;
			::g->startmap = 1;
			//DoomLib::skipToNew = true;
			//GK: Re init wad files to apply the change
			IdentifyVersion();
			D_AddFile("wads/newopt.wad");
			//GK: New pwad for compatibility with Evilution and Plutonia (only for DOOM II)
			if (::g->gamemode == commercial) {
				D_AddFile("wads/ua.wad");
			}
			// if (::g->gamemission == pack_master) {
			// 	D_AddFile("wads/mlbls.wad");
			// }
		}
	}
	p = M_CheckParm ("-file");
	if (p)
	{
		qboolean isgen = false;
		int pos[2];
		pos[0] = p;
		//GK: Check if it is having double -file parameter for both and for specific games
		int q = -1;
		int qt = -1;
		int q1= M_CheckParm("-doom");
		int q2= M_CheckParm("-doom2");
		if (::g->gamemode == retail) {
			q = q1;
			qt = q1;
		}
		else if (::g->gamemode == commercial) {
			q = q2;
			qt = q2;
		}
		int np = -1;
		if (q > p) {
			isgen = true;
			np = M_CheckParm("-file",true);
		}
		else if (q > -1 && p > q) {
			q = M_CheckParm("-both");
			if (q > p) {
				isgen = true;
				np = M_CheckParm("-file", true);
			}
		}
		pos[1] = np;
		memset(DoomLib::otherfiles, 0, sizeof(DoomLib::otherfiles));
		memset(DoomLib::generalfiles, 0, sizeof(DoomLib::generalfiles));
		int count[5] = {0,0,0,0,0};
		int c = 0;
		// the parms after p are ::g->wadfile/lump names,
		// until end of parms or another - preceded parm
		::g->modifiedgame = true;            // homebrew levels
		
		for (int i = 0; i < 2; i++) {
			int arg = 0;
			if (pos[i] == -1)
				continue;
			while (++pos[i] != ::g->myargc && ::g->myargv[pos[i]][0] != '-') {
				if (::g->gamemode == commercial) {
					if (!idStr::Icmp("ex", ::g->myargv[pos[i]])) {
						pos[i]++;
						arg = atoi(::g->myargv[pos[i]]);
						pos[i]++;
					}
					if(arg){
						if (arg == DoomLib::expansionSelected) {
							D_AddFile(::g->myargv[pos[i]]);
							continue;
						}
						else {
							DoomLib::otherfiles[arg - 1][count[arg - 1]] = D_ExtractFileName(::g->myargv[pos[i]]);
							count[arg - 1]++;
							continue;
						}
					}
				}
				if (isgen) {
					if (qt > pos[i] || pos[i]>M_CheckParm("-both")) {
						DoomLib::generalfiles[c] = D_ExtractFileName(::g->myargv[pos[i]]);
						c++;
					}
				}
				D_AddFile(::g->myargv[pos[i]]);
			}
		}
	}
	p = M_CheckParm ("-playdemo");

	if (!p)
		p = M_CheckParm ("-timedemo");

	if (p && p < ::g->myargc-1)
	{
		sprintf (file,"%s\\%s\\DEMO\\%s.lmp", cvarSystem->GetCVarString("fs_savepath"), !idStr::Icmp(cvarSystem->GetCVarString("fs_game"), "") ? "base" : cvarSystem->GetCVarString("fs_game"), ::g->myargv[p+1]);
		D_AddFile (file);
		I_Printf("Playing demo %s.lmp.\n",::g->myargv[p+1]);
	}

	::g->debugfile = M_CheckParm("-debug");


	if ( DoomLib::matchParms.gameEpisode != GAME_EPISODE_UNKNOWN ) {
		::g->startepisode = DoomLib::matchParms.gameEpisode;
		::g->autostart = 1;
	}

	if ( DoomLib::matchParms.gameMap != -1 ) {
		::g->startmap = DoomLib::matchParms.gameMap;
		::g->autostart = 1;
	}

	if ( DoomLib::matchParms.gameSkill != -1) {
		::g->startskill = (skill_t)DoomLib::matchParms.gameSkill;
	}
	if (::g->gamemode == retail || (::g->gamemode == commercial && !initonce)) {
		// get skill / episode / map from cmdline
		p = M_CheckParm("-skill");
		if (p && p < ::g->myargc - 1)
		{
			::g->startskill = (skill_t)(::g->myargv[p + 1][0] - '1');
			if (::g->startskill < sk_baby || ::g->startskill > sk_masochism)
#ifdef FOOLS
				::g->startskill = sk_masochism;
#else
				::g->startskill = sk_medium;
#endif
			::g->autostart = true;
		}

		p = M_CheckParm("-episode");
		if (p && p < ::g->myargc - 1)
		{
			::g->startepisode = ::g->myargv[p + 1][0] - '0';
			if (::g->startepisode <= 0 || ::g->startepisode > 4)
				::g->startepisode = 1;
			::g->startmap = 1;
			::g->autostart = true;
		}
	}
	//GK:Re-enable Network Related stuff
	p = M_CheckParm ("-timer");
	if (p && p < ::g->myargc - 1 && ::g->deathmatch)
	{
		
			::g->dmtime = atoi(::g->myargv[p+1]);
			//time = timeLimit;
			I_Printf("Levels will end after %d minute", time);
			if (::g->dmtime > 1)
				I_Printf("s");
			I_Printf(".\n");
	}
	else {
		// GK:Give by default 1min
		const int timeLimit = 60;
		::g->dmtime = timeLimit;
	}
	p = M_CheckParm ("-avg");
	if (p && p < ::g->myargc-1 && ::g->deathmatch)
		I_Printf("Austin Virtual Gaming: Levels will end after 20 minutes\n");
	
	I_Printf ("Z_Init: Init zone memory allocation daemon. \n");
	//GK: Allow the user to set the Z-Memory (What Could Possibly go wrong?)
	int zcheck = M_CheckParm("-zmem");
	::g->zmem = 30;
	if (zcheck) {
		::g->zmem = atoi(::g->myargv[zcheck + 1]);
	}
	Z_Init ();

	// init subsystems
	I_Printf ("V_Init: allocate ::g->screens.\n");
	V_Init ();

	I_Printf ("M_LoadDefaults: Load system defaults.\n");
	M_LoadDefaults ();              // load before initing other systems

	I_Printf ("W_Init: Init WADfiles.\n");
	W_InitMultipleFiles (wadfiles);

	idList<int> expinfoPointer = W_GetNumsForName("EXPINFO");
	if (expinfoPointer[0] >= 0) {
		int expPrev = 0;
		for (int i = 0; i < expinfoPointer.Num(); i++) {
			if (expinfoPointer[i] > expPrev && expinfoPointer[i] < W_GetLumpCount()) {
				::g->gamemission = pack_custom;
				expPrev = expinfoPointer[i];
				EX_add(expinfoPointer[i]);
			}
		}
	}

	idList<int> dehackeds = W_GetNumsForName("DEHACKED");
	if (dehackeds[0] >= 0) {
		int prev = 0;
		for (int i = 0; i < dehackeds.Num(); i++) {
			if (dehackeds[i] >= prev && dehackeds[i] < W_GetLumpCount()) {
				prev = dehackeds[i];
				loaddeh(dehackeds[i]);
			}
		}
	}
	Sys_ChangeTitle(::g->title);
	I_Printf("                        %s                           \n", ::g->title);

	if (::g->gamemode == retail || (::g->gamemode == commercial && !initonce)) {
		p = M_CheckParm("-warp");
		if (p && p < ::g->myargc - 1)
		{
			::g->warpUsed = true;
			int level = atoi(::g->myargv[p + 1]);
			int episode = ::g->myargv[p + 1][0] - '0';
			int map = 1;
			if (strlen(::g->myargv[p + 1]) > 1) {
				map = ::g->myargv[p + 1][1] - '0';
			}
			else {
				int cp = p + 2;
				if (cp < ::g->myargc) {
					map = ::g->myargv[p + 2][0] - '0';
				}
			}

			
			if (::g->gamemode == commercial) {
				::g->startmap = level;
				if (::g->gamemission == pack_master) {
					DoomLib::use_doomit = true;
					if (level <= 0 || level > 21) {
						::g->startmap = 1;
					}
				}
				else
					if (::g->gamemission == pack_nerve) {
						if (level <= 0 || level > 9) {
							::g->startmap = 1;
						}
					}
					else
					{
						if (level <= 0 || level > 33) {
							::g->startmap = 1;
						}

					}
			}
			else
			{
				::g->startepisode = episode;
				if (episode <= 0 || episode > 4)
					::g->startepisode = 1;
				::g->startmap = map;
				if (map <= 0 || map > 9)
					::g->startmap = 1;
			}

			if (::g->gamemission == pack_custom)
			{
				if (::g->gamemode == commercial)
				{
					::g->startmap = level;
					if (level <= 0 || level > (int)::g->maps.size())
					{
						::g->startmap = 1;
					}
				}
				else
				{
					::g->startepisode = episode;
					if (episode <= 0 || episode > (int)::g->clusters.size())
						::g->startepisode = 1;
					::g->startmap = map;
					if (::g->clusters[::g->startepisode - 1].startmap != ::g->clusters[::g->startepisode - 1].endmap)
						if (map <= 0 || ::g->clusters[::g->startepisode - 1].startmap - 1 + map > ::g->clusters[::g->startepisode - 1].endmap + 1)
							::g->startmap = 1;
				}
			}

			::g->autostart = true;
		}
	}


	// Check for -file in shareware
	if (::g->modifiedgame)
	{
		// These are the lumps that will be checked in IWAD,
		// if any one is not present, execution will be aborted.
		char name[23][16]=
		{
			"e2m1","e2m2","e2m3","e2m4","e2m5","e2m6","e2m7","e2m8","e2m9",
				"e3m1","e3m3","e3m3","e3m4","e3m5","e3m6","e3m7","e3m8","e3m9",
				"dphoof","bfgga0","heada1","cybra1","spida1d1"
		};
		int i;

		if ( ::g->gamemode == shareware)
			I_Error("\nYou cannot -file with the shareware "
			"version. Register!");

		// Check for fake IWAD with right name,
		// but w/o all the lumps of the registered version. 
		if (::g->gamemode == registered)
			for (i = 0;i < 23; i++)
				if (W_CheckNumForName(name[i])<0)
					I_Error("\nThis is not the registered version.");
	}

	// Iff additonal PWAD files are used, print modified banner
	if (::g->modifiedgame)
	{
		/*m*/I_Printf (
		"===========================================================================\n"
			"ATTENTION:  This version of DOOM has been modified.  If you would like to\n"
			"get a copy of the original game, call 1-800-IDGAMES or see the readme file.\n"
			"        You will not receive technical support for modified games.\n"
			"                      press enter to continue\n"
			"===========================================================================\n"
			);
		getchar ();
	}


	// Check and print which version is executed.
	switch ( ::g->gamemode )
	{
	case shareware:
	case indetermined:
		I_Printf (
			"===========================================================================\n"
			"                                Shareware!\n"
			"===========================================================================\n"
			);
		break;
	case registered:
	case retail:
	case commercial:
		I_Printf (
			"===========================================================================\n"
			"                 Commercial product - do not distribute!\n"
			"         Please report software piracy to the SPA: 1-800-388-PIR8\n"
			"===========================================================================\n"
			);
		break;

	default:
		// Ouch.
		break;
	}

	I_Printf ("M_Init: Init miscellaneous info.\n");
	M_Init ();

	I_Printf ("R_Init: Init DOOM refresh daemon - ");
	R_Init ();

	I_Printf ("\nP_Init: Init Playloop state.\n");
	P_Init ();

	I_Printf ("I_Init: Setting up machine state.\n");
	I_Init ();

	I_Printf ("D_CheckNetGame: Checking network game status.\n");
	D_CheckNetGame ();
	//GK: "Extract" the Master Levels in order to be used in other ports
	int exm = M_CheckParm("-extractMaster");
	if (exm && DoomLib::hexp[2]) {
		MasterExport();
	}


	
}

void D_SetupAdditionalInfo() {
	switch (::g->gamemission) {
	case doom:
		::g->acronymPrefix = "UD";
		break;
	case doom2:
		::g->acronymPrefix = "D2";
		break;
	case pack_nerve:
		::g->acronymPrefix = "D2:NERVE";
		break;
	case pack_tnt:
		::g->acronymPrefix = "FD:TNT";
		break;
	case pack_plut:
		::g->acronymPrefix = "FD:PLUT";
		break;
	case pack_master:
		::g->acronymPrefix = "D2:MASTER";
		break;

	}
}

bool D_DoomMainPoll(void)
{
	int             p;
	char                    file[256];

	if (D_PollNetworkStart() == false)
		return false;


	I_Printf( "S_Init: Setting up sound.\n" );
	S_Init( s_volume_sound.GetInteger(), s_volume_midi.GetInteger() );

	I_Printf ("HU_Init: Setting up heads up display.\n");
	HU_Init ();

	I_Printf ("ST_Init: Init status bar.\n");
	ST_Init ();

	M_SizeDisplay(0);

	// start the apropriate game based on parms
	p = M_CheckParm ("-record");

	if (p && p < ::g->myargc-1)
	{
		G_RecordDemo (::g->myargv[p+1]);
		::g->autostart = true;
	}

	p = M_CheckParm ("-playdemo");
	if (p && p < ::g->myargc-1)
	{
		//::g->singledemo = true;              // quit after one demo
		G_DeferedPlayDemo (::g->myargv[p+1]);
		//D_DoomLoop ();  // never returns
	}

	p = M_CheckParm ("-timedemo");
	if (p && p < ::g->myargc-1)
	{
		G_TimeDemo (::g->myargv[p+1]);//"nukage1");
		//D_DoomLoop ();  // never returns
	}

	p = M_CheckParm ("-loadgame");
	if (p && p < ::g->myargc-1)
	{
		if (M_CheckParm("-cdrom"))
			sprintf(file, "c:\\doomdata\\" SAVEGAMENAME "%c.dsg",::g->myargv[p+1][0]);
		else
			sprintf(file, SAVEGAMENAME "%c.dsg",::g->myargv[p+1][0]);
		G_LoadGame (file);
	}

	D_SetupAdditionalInfo();
	if ( ::g->gameaction != ga_loadgame && ::g->gameaction != ga_playdemo )
	{
		if (::g->autostart || ::g->netgame ) {
			if (::g->gamemode == commercial && !initonce) {
				initonce = true;
			}
			G_InitNew (::g->startskill, ::g->startepisode, ::g->startmap );
		} else if(  ::g->gameaction != ga_newgame) {
			if (::g->gamemode == commercial && !initonce) {
				initonce = true;
				M_ChangeMenuExp(cl_expMenu.GetInteger());
			}
			if (::op) {
				::op->SetAdditionalInfo("status", va("%s:Title Menu", ::g->acronymPrefix));
				if (::g->gamemode == retail || ::g->gamemode == shareware || ::g->gamemode == registered) {
					::op->SetAdditionalInfo("large image", "ud");
				}
				else if (::g->gamemode == commercial) {
					::op->SetAdditionalInfo("large image", "d2");
				}
			}
			D_StartTitle ();                // start up intro loop
		}
	}
	return true;
}

char* D_ExtractFileName(const char* file) {
	char* fname = strtok(strdup(file), "\\");
	while (fname) {
		char* tname = strtok(NULL, "\\");
		if (tname) {
			fname = tname;
		}
		else {
			break;
		}
	}
	return fname;
}
