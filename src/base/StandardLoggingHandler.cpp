//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2012 Ulrich von Zadow
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//  Current versions can be found at www.libavg.de

#include "StandardLoggingHandler.h"
#include "Logger.h"

#include <iostream>
#include <iomanip>

using namespace std;

namespace avg
{
using namespace logging;

StandardLoggingHandler::StandardLoggingHandler()
{

}

StandardLoggingHandler::~StandardLoggingHandler()
{

}

void StandardLoggingHandler::logMessage(const tm* pTime, unsigned millis,
        const string& category, long level, const UTF8String& sMsg)
{
    char timeString[256];
    strftime(timeString, sizeof(timeString), "%y-%m-%d %H:%M:%S", pTime);
    cerr << "[" << timeString << "." << 
        setw(3) << setfill('0') << millis << setw(0) << "][";
    cerr << logging::levelToString(level) << "][";
    cerr << category << "] : " << sMsg << endl;
    cerr.flush();
}

}
