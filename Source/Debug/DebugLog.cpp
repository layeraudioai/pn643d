/*
Copyright (C) 2001 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"

#include "DebugLog.h"
#include "Dump.h"
#include "DBGConsole.h"

#include "Utility/IO.h"

#include <stdio.h>
#include <stdarg.h>

#ifdef DAEDALUS_LOG

//*****************************************************************************
//
//*****************************************************************************
static bool			g_bLog = false;
static FILE *		g_hOutputLog	= NULL;

//*****************************************************************************
//
//*****************************************************************************
bool Debug_InitLogging()
{
	IO::Filename log_filename;

#ifdef DAEDALUS_ENABLE_SDMC_DIAGNOSTICS
	const char * const log_directory = "sdmc:/3ds/DaedalusX64";
	if (!IO::Directory::EnsureExists(log_directory))
	{
		printf("Unable to create SD diagnostics directory: %s\n", log_directory);
		return true; // Diagnostics are optional; do not prevent the emulator starting.
	}
	snprintf(log_filename, sizeof(log_filename), "%s/diagnostics.log", log_directory);
	g_hOutputLog = fopen(log_filename, "w");
#else
	Dump_GetDumpDirectory(log_filename, "");
	IO::Path::Append(log_filename, "daedalus.txt");
	g_hOutputLog = fopen(log_filename, "w");
#endif

#ifdef DAEDALUS_DEBUG_CONSOLE
	if (CDebugConsole::IsAvailable())
		CDebugConsole::Get()->Msg(0, "Writing diagnostics to '%s'", log_filename);
#endif

	g_bLog = (g_hOutputLog != NULL);
	if (g_bLog)
	{
		fprintf(g_hOutputLog, "DaedalusX64 diagnostic log\n");
#ifdef DAEDALUS_ENABLE_SDMC_DIAGNOSTICS
		fprintf(g_hOutputLog, "Logging: high-level emulator events; function profile snapshots every 300 VBlanks.\n");
		fprintf(g_hOutputLog, "Detailed per-memory-access tracing is intentionally disabled to avoid unusable log volume.\n");
#endif
		fflush(g_hOutputLog);
	}
#ifdef DAEDALUS_ENABLE_SDMC_DIAGNOSTICS
	if (!g_bLog)
		printf("Unable to open SD diagnostics log: %s\n", log_filename);
	return true; // A missing/unwritable SD card must not disable the emulator.
#else
	return g_bLog;
#endif
}

//*****************************************************************************
//
//*****************************************************************************
void Debug_FinishLogging()
{
	if( g_hOutputLog )
	{
		fclose( g_hOutputLog );
		g_hOutputLog = NULL;
	}
}

//*****************************************************************************
//
//*****************************************************************************
void Debug_Print( const char * format, ... )
{
	if(g_bLog && format != NULL )
	{
		char buffer[1024+1];
		va_list va;
		va_start(va, format);
		vsnprintf(buffer, sizeof(buffer), format, va);
		va_end(va);

		if (g_hOutputLog != NULL)
		{
			fprintf(g_hOutputLog, "%s\n", buffer);
			// Diagnostic mode favors recoverable logs over throughput.
			fflush(g_hOutputLog);
		}
	}
}

//*****************************************************************************
//
//*****************************************************************************
bool		Debug_GetLoggingEnabled()
{
	return g_bLog && (g_hOutputLog != NULL);
}

//*****************************************************************************
//
//*****************************************************************************
void		Debug_SetLoggingEnabled( bool enabled )
{
	g_bLog = enabled;
}


#endif // DAEDALUS_LOG
