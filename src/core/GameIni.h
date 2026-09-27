#pragma once

#include <string>

// The game's own ini: sudtest.ini, sudfmrm.ini, blackgold.ini, ... - whichever
// one loads this dll.
namespace GameIni
{
    // The ini whose SSDraw entry loads this dll, else the first candidate found.
    // Empty if there is none.
    std::string Path();

    // Folder of Path(), without trailing slash. Empty if there is no ini.
    std::string Dir();

    // Empty if the ini, the section or the key is missing.
    std::string Read(const char* section, const char* key);

    bool Write(const char* section, const char* key, const std::string& value);
}
