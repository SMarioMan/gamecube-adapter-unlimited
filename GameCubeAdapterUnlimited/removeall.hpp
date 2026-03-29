#pragma once
#include <windows.h>

bool IsRunningAsAdmin();

// Returns EXIT_OK (0), EXIT_REBOOT (1), EXIT_FAIL (2), or EXIT_USAGE (3)
int RemoveAll(LPCTSTR baseName, int argc, LPTSTR argv[]);