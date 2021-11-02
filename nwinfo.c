// SPDX-License-Identifier: Unlicense

#include <stdio.h>
#include <windows.h>
#include <VersionHelpers.h>
#include "nwinfo.h"

int __cdecl main(int argc, char** argv)
{
	if (!IsWindows7OrGreater())
	{
		printf("You need at least Windows 7\n");
		return 0;
	}
	for (int i = 0; i < argc; i++) {
		if (i == 0 && argc > 1)
			continue;
		else if (_stricmp(argv[i], "--sys") == 0)
			nwinfo_sys();
		else if (_stricmp(argv[i], "--cpu") == 0)
			nwinfo_cpuid();
		else if (_strnicmp(argv[i], "--net", 5) == 0) {
			if (_stricmp(&argv[i][5], "=active") == 0)
				nwinfo_network(1);
			else
				nwinfo_network(0);
		}
		else if (_stricmp(argv[i], "--acpi") == 0)
			nwinfo_acpi();
		else if (_stricmp(argv[i], "--smbios") == 0)
			nwinfo_smbios();
		else if (_stricmp(argv[i], "--disk") == 0)
			nwinfo_disk();
		else {
			printf("Usage: nwinfo OPTIONS\n");
			printf("OPTIONS:\n");
			printf("  --sys          Print system info.\n");
			printf("  --cpu          Print CPUID info.\n");
			printf("  --net          Print network info.\n");
			printf("  --net=active   Print active network info\n");
			printf("  --acpi         Print ACPI info.\n");
			printf("  --smbios       Print SMBIOS info.\n");
			printf("  --disk         Print disk info.\n");
		}
	}
	return 0;
}
