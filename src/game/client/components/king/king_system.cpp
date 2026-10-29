/* KING client: operating system tweaks (process priorities). Windows only, no-ops elsewhere. */
#include "king_system.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <tlhelp32.h>

#include <cwchar>

bool KingSetHighPriority(bool High)
{
	return SetPriorityClass(GetCurrentProcess(), High ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS) != 0;
}

int KingSetDiscordLowPriority(bool Low)
{
	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(Snapshot == INVALID_HANDLE_VALUE)
		return 0;
	int Changed = 0;
	PROCESSENTRY32W Entry;
	Entry.dwSize = sizeof(Entry);
	if(Process32FirstW(Snapshot, &Entry))
	{
		do
		{
			const wchar_t *pName = Entry.szExeFile;
			if(_wcsicmp(pName, L"Discord.exe") == 0 || _wcsicmp(pName, L"DiscordPTB.exe") == 0 || _wcsicmp(pName, L"DiscordCanary.exe") == 0)
			{
				HANDLE Process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, Entry.th32ProcessID);
				if(Process)
				{
					if(SetPriorityClass(Process, Low ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS))
						Changed++;
					CloseHandle(Process);
				}
			}
		} while(Process32NextW(Snapshot, &Entry));
	}
	CloseHandle(Snapshot);
	return Changed;
}

#else

bool KingSetHighPriority(bool High)
{
	(void)High;
	return false;
}

int KingSetDiscordLowPriority(bool Low)
{
	(void)Low;
	return 0;
}

#endif
