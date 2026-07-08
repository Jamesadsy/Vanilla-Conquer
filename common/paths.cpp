// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection
#include "paths.h"
#include "debugstring.h"
#include "ini.h"
#include "rawfile.h"
#include <stdlib.h>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

PathsClass Paths;

void PathsClass::Init(const char* suffix, const char* ini_name, const char* data_name, const char* cmd_arg)
{
    if (suffix != nullptr) {
        Suffix = suffix;
    }

    // Check the argv[0] arg assuming it was passed. This may be a symlink so should be checked.
    std::string argv_path;

    if (cmd_arg != nullptr) {
        argv_path = Argv_Path(cmd_arg);
    }

    // Calls with unused returns to set the default variable values if not already set.
    Program_Path();
    Data_Path();
    User_Path();

    DBG_INFO("Searching the following paths for path config data:\n  argv: '%s'\n  binary: '%s'\n  default data: "
             "'%s'\n  default user: '%s'",
             argv_path.c_str(),
             ProgramPath.c_str(),
             DataPath.c_str(),
             UserPath.c_str());

    // Check for a config file to load paths from.
    RawFileClass file;
    bool use_argv_path = false;
    bool use_prog_path = false;

    if (ini_name != nullptr) {
        if (!argv_path.empty() && RawFileClass(Concatenate_Paths(argv_path.c_str(), ini_name).c_str()).Is_Available()) {
            file.Set_Name(Concatenate_Paths(argv_path.c_str(), ini_name).c_str());
            use_argv_path = true;
        } else if (RawFileClass(Concatenate_Paths(ProgramPath.c_str(), ini_name).c_str()).Is_Available()) {
            file.Set_Name(Concatenate_Paths(ProgramPath.c_str(), ini_name).c_str());
            use_prog_path = true;
        } else if (RawFileClass(Concatenate_Paths(UserPath.c_str(), ini_name).c_str()).Is_Available()) {
            file.Set_Name(Concatenate_Paths(UserPath.c_str(), ini_name).c_str());
        } else if (RawFileClass(Concatenate_Paths(DataPath.c_str(), ini_name).c_str()).Is_Available()) {
            file.Set_Name(Concatenate_Paths(DataPath.c_str(), ini_name).c_str());
        }
    }

    // Next we check if the data path is either the argv or program path.
    bool have_argv_data = false;
    bool have_prog_data = false;

    if (data_name != nullptr) {
        if (!argv_path.empty()
            && RawFileClass(Concatenate_Paths(argv_path.c_str(), data_name).c_str()).Is_Available()) {
            have_argv_data = true;
        } else if (RawFileClass(Concatenate_Paths(ProgramPath.c_str(), data_name).c_str()).Is_Available()) {
            have_prog_data = true;
        }
    }

    INIClass ini;
    ini.Load(file);

    const char section[] = "Paths";
    char buffer[128]; // TODO max ini line size.

    // Even if the config was found with the binary, we still check to see if it gives use alternative paths.
    // If not, assume we are in portable mode and point the DataPath to ProgramPath.
    if (ini.Get_String(section, "DataPath", "", buffer, sizeof(buffer)) < sizeof(buffer) && buffer[0] != '\0') {
        DataPath = buffer;
    } else if (have_argv_data) {
        DataPath = argv_path;
    } else if (have_prog_data) {
        DataPath = ProgramPath;
    }

    // Same goes for the UserPath.
    if (ini.Get_String(section, "UserPath", "", buffer, sizeof(buffer)) < sizeof(buffer) && buffer[0] != '\0') {
        UserPath = buffer;
    }
#if defined(__APPLE__) && TARGET_OS_IOS
    // iOS: the .app bundle (ProgramPath) is READ-ONLY, so "portable mode" (user data
    // stored next to the binary) cannot work -- every write would fail. Keep UserPath at
    // the writable sandbox Documents dir already set by User_Path(). An explicit [Paths]
    // UserPath override (handled above) still wins. Without this guard, finding CONQUER.INI
    // in the bundle sets use_prog_path/use_argv_path and clobbers UserPath with the
    // read-only bundle -> "Error saving game!" and settings write-backs to a read-only path.
    else {
        DBG_INFO("iOS: keeping sandbox UserPath (bundle is read-only), skipping portable-mode override");
    }
#else
    else if (use_argv_path) {
        UserPath = argv_path;
    } else if (use_prog_path) {
        UserPath = ProgramPath;
    }
#endif

    DBG_INFO("Read only data directory is set to '%s'", DataPath.c_str());
    DBG_INFO("Read/Write user data directory is set to '%s'", UserPath.c_str());
}
