// SPDX-License-Identifier: Unlicense

#include <windows.h>
#include <winbase.h>
#include <stdio.h>
#include <stdlib.h>

#include "libnw.h"
#include "utils.h"

typedef struct _SYSTEM_BOOT_ENVIRONMENT_INFORMATION
{
	GUID BootIdentifier;
	FIRMWARE_TYPE FirmwareType;
	union
	{
		ULONGLONG BootFlags;
		struct
		{
			ULONGLONG DbgMenuOsSelection : 1;
			ULONGLONG DbgHiberBoot : 1;
			ULONGLONG DbgSoftBoot : 1;
			ULONGLONG DbgMeasuredLaunch : 1;
			ULONGLONG DbgMeasuredLaunchCapable : 1;
			ULONGLONG DbgSystemHiveReplace : 1;
			ULONGLONG DbgMeasuredLaunchSmmProtections : 1;
			ULONGLONG DbgMeasuredLaunchSmmLevel : 7;
		};
	};
} SYSTEM_BOOT_ENVIRONMENT_INFORMATION, * PSYSTEM_BOOT_ENVIRONMENT_INFORMATION;

#define SystemBootEnvironmentInformation 90

static const char* GV_GUID = "{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";

static const CHAR* Win10BuildNumber(DWORD dwBuildNumber)
{
	switch (dwBuildNumber)
	{
	case 22621U:
		return "11 22H2"; // Sun Valley 2
	case 22000U:
		return "11 21H2"; // Sun Valley
	case 19045U:
		return "10 22H2"; // Vibranium 22H2
	case 19044U:
		return "10 21H2"; // Vibranium 21H2
	case 19043U:
		return "10 21H1"; // Vibranium 21H1
	case 19042U:
		return "10 20H2"; // Vibranium 20H2
	case 19041U:
		return "10 2004"; // Vibranium 20H1
	case 18363U:
		return "10 1909"; // Vanadium 19H2
	case 18362U:
		return "10 1903"; // 19H1
	case 17763U:
		return "10 1809"; // Redstone 5
	case 17134U:
		return "10 1803"; // Redstone 4
	case 16299U:
		return "10 1709"; // Redstone 3
	case 15063U:
		return "10 1703"; // Redstone 2
	case 14393U:
		return "10 1607"; // Redstone
	case 10586U:
		return "10 1511"; // Threshold 2
	case 10240U:
		return "10 1507"; // Threshold
	}
	if (dwBuildNumber >= 22000U)
		return "11";
	return "10";
}

static const CHAR* WinServer2016BuildNumber(DWORD dwBuildNumber)
{
	switch (dwBuildNumber)
	{
	case 20348U:
		return "Server 2022";
	case 19042U:
		return "Server, version 20H2"; // WTF?
	case 19041U:
		return "Server, version 2004"; // WTF?
	case 18363U:
		return "Server, version 1909"; // WTF?
	case 18362U:
		return "Server, version 1903"; // WTF?
	case 17763U:
		return "Server 2019"; // Server, version 1809 ?
	case 17134U:
		return "Server, version 1803"; // WTF?
	case 16299U:
		return "Server, version 1709"; // WTF?
	case 14393U:
		return "Server 2016";
	}
	if (dwBuildNumber >= 20348U)
		return "Server 2022";
	else if (dwBuildNumber >= 17763U)
		return "Server 2019";
	return "Server 2016";
}

static const CHAR*
OsVersionToStr(OSVERSIONINFOEXW* p)
{
	if (p->dwMajorVersion == 10 && p->dwMinorVersion == 0)
	{
		// FUCK YOU MICROSOFT
		if (p->wProductType != VER_NT_WORKSTATION)
			return WinServer2016BuildNumber(p->dwBuildNumber);
		else
			return Win10BuildNumber(p->dwBuildNumber);
	}
	if (p->dwMajorVersion == 6 && p->dwMinorVersion == 3)
	{
		if (p->wProductType != VER_NT_WORKSTATION)
			return "Server 2012 R2";
		else
			return "8.1";
	}
	if (p->dwMajorVersion == 6 && p->dwMinorVersion == 2)
	{
		if (p->wProductType != VER_NT_WORKSTATION)
			return "Server 2012";
		else
			return "8";
	}
	if (p->dwMajorVersion == 6 && p->dwMinorVersion == 1)
	{
		if (p->wProductType != VER_NT_WORKSTATION)
			return "Server 2008 R2";
		else
			return "7";
	}
	if (p->dwMajorVersion == 6 && p->dwMinorVersion == 0)
	{
		if (p->wProductType != VER_NT_WORKSTATION)
			return "Server 2008";
		else
			return "Vista";
	}
	if (p->dwMajorVersion == 5 && p->dwMinorVersion == 2)
	{
		if (p->wSuiteMask & VER_SUITE_WH_SERVER)
			return "Home Server";
		else if (p->wProductType == VER_NT_WORKSTATION)
			return "XP x64";
		else
			return "Server 2003";
	}
	if (p->dwMajorVersion == 5 && p->dwMinorVersion == 1)
	{
		return "XP";
	}
#if 0
	if (p->dwMajorVersion == 5 && p->dwMinorVersion == 0)
	{
		return "2000";
	}
#endif
	return "Unknown";
}

static void PrintOsVer(PNODE node)
{
	NTSTATUS(WINAPI * RtlGetVersion)(LPOSVERSIONINFOEXW) = NULL;
	OSVERSIONINFOEXW osInfo = { 0 };
	HMODULE hMod = GetModuleHandleA("ntdll");

	if (hMod)
		*(FARPROC*)&RtlGetVersion = GetProcAddress(hMod, "RtlGetVersion");

	if (RtlGetVersion)
	{
		CHAR szSP[] = " SP65535.65535";
		osInfo.dwOSVersionInfoSize = sizeof(osInfo);
		RtlGetVersion(&osInfo);
		if (osInfo.wServicePackMinor)
			snprintf(szSP, sizeof(szSP), " SP%u.%u",
				osInfo.wServicePackMajor, osInfo.wServicePackMinor);
		else if (osInfo.wServicePackMajor)
			snprintf(szSP, sizeof(szSP), " SP%u", osInfo.wServicePackMajor);
		else
			szSP[0] = '\0';
		NWL_NodeAttrSetf(node, "OS", 0, "Windows %s%s", OsVersionToStr(&osInfo), szSP);
		NWL_NodeAttrSetf(node, "Build Number", 0, "%lu.%lu.%lu",
			osInfo.dwMajorVersion, osInfo.dwMinorVersion, osInfo.dwBuildNumber);
	}
}

static void PrintUptime(PNODE node)
{
	UINT64 Uptime = GetTickCount64();
	UINT64 Days = Uptime / 1000ULL / 3600ULL / 24ULL;
	UINT64 Hours = Uptime / 1000ULL / 3600ULL - Days * 24ULL;
	UINT64 Minutes = Uptime / 1000ULL / 60ULL - Days * 24ULL * 60ULL - Hours * 60ULL;
	UINT64 Seconds = Uptime / 1000ULL - Days * 24ULL * 3600ULL - Hours * 3600ULL - Minutes * 60ULL;
	NWL_NodeAttrSetf(node, "Uptime", 0, "%llu days, %llu hours, %llu min, %llu sec", Days, Hours, Minutes, Seconds);
}

static void PrintOsInfo(PNODE node)
{
	DWORD bufCharCount = NWINFO_BUFSZ;
	SYSTEM_INFO SystemInfo;
	char* infoBuf = NWLC->NwBuf;
	if (GetComputerNameA(infoBuf, &bufCharCount))
		NWL_NodeAttrSet(node, "Computer Name", infoBuf, 0);
	bufCharCount = NWINFO_BUFSZ;
	if (GetUserNameA(infoBuf, &bufCharCount))
		NWL_NodeAttrSet(node, "Username", infoBuf, 0);
	if (GetComputerNameExA(ComputerNameDnsDomain, infoBuf, &bufCharCount))
		NWL_NodeAttrSet(node, "DNS Domain", infoBuf, 0);
	if (GetComputerNameExA(ComputerNameDnsHostname, infoBuf, &bufCharCount))
		NWL_NodeAttrSet(node, "DNS Hostname", infoBuf, 0);
	if (GetSystemDirectoryA(infoBuf, NWINFO_BUFSZ))
		NWL_NodeAttrSet(node, "System Directory", infoBuf, 0);
	if (GetWindowsDirectoryA(infoBuf, NWINFO_BUFSZ))
		NWL_NodeAttrSet(node, "Windows Directory", infoBuf, 0);
	PrintUptime(node);
	GetNativeSystemInfo(&SystemInfo);
	switch (SystemInfo.wProcessorArchitecture)
	{
	case PROCESSOR_ARCHITECTURE_AMD64:
		NWL_NodeAttrSet(node, "Processor Architecture", "x64", 0);
		break;
	case PROCESSOR_ARCHITECTURE_INTEL:
		NWL_NodeAttrSet(node, "Processor Architecture", "x86", 0);
		break;
	default:
		NWL_NodeAttrSet(node, "Processor Architecture", "UNKNOWN", 0);
		break;
	}
	NWL_NodeAttrSetf(node, "Page Size", NAFLG_FMT_NUMERIC, "%u", SystemInfo.dwPageSize);
}

static void PrintSysMetrics(PNODE node)
{
	NWL_NodeAttrSetBool(node, "Safe Mode", GetSystemMetrics(SM_CLEANBOOT), 0);
	NWL_NodeAttrSetBool(node, "DBCS Enabled", GetSystemMetrics(SM_DBCSENABLED), 0);
	NWL_NodeAttrSetBool(node, "Debug Version", GetSystemMetrics(SM_DEBUG), 0);
	NWL_NodeAttrSetBool(node, "Slow Processor", GetSystemMetrics(SM_SLOWMACHINE), 0);
	NWL_NodeAttrSetBool(node, "Network Presence", GetSystemMetrics(SM_NETWORK) & 0x01, 0);
}

static void PrintFwInfo(PNODE node)
{
	DWORD VarSize = 0;
	UINT8 SecureBoot = 0;
	BOOL IsUefi = FALSE;

	SYSTEM_BOOT_ENVIRONMENT_INFORMATION BootInfo = { 0 };
	if (NWL_NtQuerySystemInformation(SystemBootEnvironmentInformation, &BootInfo, sizeof(BootInfo), NULL))
	{
		NWL_NodeAttrSet(node, "Boot Identifier", NWL_WinGuidToStr(&BootInfo.BootIdentifier), NAFLG_FMT_GUID);
		IsUefi = (BootInfo.FirmwareType == FirmwareTypeUefi);
	}
	else
	{
		NWL_GetFirmwareEnvironmentVariable("", "{00000000-0000-0000-0000-000000000000}", NULL, 0);
		IsUefi = (GetLastError() != ERROR_INVALID_FUNCTION);
	}
	NWL_NodeAttrSet(node, "Firmware", IsUefi ? "UEFI" : "Legacy BIOS", 0);

	if (IsUefi)
	{
		VarSize = NWL_GetFirmwareEnvironmentVariable("SecureBoot", GV_GUID, &SecureBoot, sizeof(UINT8));
		if (VarSize)
			NWL_NodeAttrSetf(node, "Secure Boot", 0, "%s", SecureBoot ? "ENABLED" : "DISABLED");
		else
			NWL_NodeAttrSet(node, "Secure Boot", "UNSUPPORTED", 0);
	}
}

typedef struct _TPM_DEVICE_INFO
{
	UINT32 structVersion;
	UINT32 tpmVersion;
	UINT32 tpmInterfaceType;
	UINT32 tpmImpRevision;
} TPM_DEVICE_INFO, *PTPM_DEVICE_INFO;

static const CHAR*
TpmVersion(UINT32 ver)
{
	switch (ver)
	{
	case 1: return "v1.2";
	case 2: return "v2.0";
	}
	return "UNKNOWN";
}

static void PrintTpmInfo(PNODE node)
{
	UINT32 (WINAPI *GetTpmInfo) (UINT32 Size, VOID *Info) = NULL;
	HMODULE hL = LoadLibraryA("tbs.dll");
	TPM_DEVICE_INFO tpmInfo = { 0 };
	struct acpi_table_header* AcpiHdr = NULL;
	AcpiHdr = NWL_GetAcpi('2MPT');
	if (AcpiHdr)
		NWL_NodeAttrSet(node, "TPM", "v2.0", 0);
	else
	{
		AcpiHdr = NWL_GetAcpi('APCT');
		if (AcpiHdr)
			NWL_NodeAttrSet(node, "TPM", "v1.2", 0);
	}
	if (AcpiHdr)
	{
		free(AcpiHdr);
		return;
	}
	if (hL)
		*(FARPROC*)&GetTpmInfo = GetProcAddress(hL, "Tbsi_GetDeviceInfo");
	if (GetTpmInfo) {
		UINT32 dwRet = GetTpmInfo(sizeof(tpmInfo), &tpmInfo);
		NWL_NodeAttrSetf(node, "TPM", 0, "%s", (dwRet == 0) ? TpmVersion(tpmInfo.tpmVersion) : "NOT FOUND");
	}
	else
		NWL_NodeAttrSet(node, "TPM", "UNSUPPORTED", 0);
}

static const char* mem_human_sizes[6] =
{ "B", "K", "M", "G", "T", "P", };

static void PrintMemInfo(PNODE node)
{
	PNODE nphy, npage;
	MEMORYSTATUSEX statex = { 0 };
	statex.dwLength = sizeof(statex);
	GlobalMemoryStatusEx(&statex);
	NWL_NodeAttrSetf(node, "Memory Usage", 0, "%u%%", statex.dwMemoryLoad);
	nphy = NWL_NodeGetChild(node, "Physical Memory");
	if (!nphy)
		nphy = NWL_NodeAppendNew(node, "Physical Memory", NFLG_ATTGROUP);
	NWL_NodeAttrSet(nphy, "Free", NWL_GetHumanSize(statex.ullAvailPhys, mem_human_sizes, 1024), NAFLG_FMT_HUMAN_SIZE);
	NWL_NodeAttrSet(nphy, "Total", NWL_GetHumanSize(statex.ullTotalPhys, mem_human_sizes, 1024), NAFLG_FMT_HUMAN_SIZE);
	npage = NWL_NodeGetChild(node, "Paging File");
	if (!npage)
		npage = NWL_NodeAppendNew(node, "Paging File", NFLG_ATTGROUP);
	NWL_NodeAttrSet(npage, "Free", NWL_GetHumanSize(statex.ullAvailPageFile, mem_human_sizes, 1024), NAFLG_FMT_HUMAN_SIZE);
	NWL_NodeAttrSet(npage, "Total", NWL_GetHumanSize(statex.ullTotalPageFile, mem_human_sizes, 1024), NAFLG_FMT_HUMAN_SIZE);
}

static void PrintBootInfo(PNODE node)
{
	DWORD dwType;
	HANDLE hFile;
	WCHAR wArcName[MAX_PATH];
	WCHAR* pFwBootDev = NWL_NtGetRegValue(HKEY_LOCAL_MACHINE,
		L"SYSTEM\\CurrentControlSet\\Control", L"FirmwareBootDevice", &dwType);
	WCHAR* pSysBootDev = NWL_NtGetRegValue(HKEY_LOCAL_MACHINE,
		L"SYSTEM\\CurrentControlSet\\Control", L"SystemBootDevice", &dwType);
	WCHAR* pStartOption = NWL_NtGetRegValue(HKEY_LOCAL_MACHINE,
		L"SYSTEM\\CurrentControlSet\\Control", L"SystemStartOptions", &dwType);
	if (pFwBootDev)
	{
		swprintf(wArcName, MAX_PATH, L"\\ArcName\\%s", pFwBootDev);
		free(pFwBootDev);
		hFile = NWL_NtCreateFile(wArcName, FALSE);
		NWL_NodeAttrSet(node, "Boot Device", NWL_NtGetPathFromHandle(hFile), 0);
		CloseHandle(hFile);
	}
	if (pSysBootDev)
	{
		swprintf(wArcName, MAX_PATH, L"\\ArcName\\%s", pSysBootDev);
		free(pSysBootDev);
		hFile = NWL_NtCreateFile(wArcName, FALSE);
		NWL_NodeAttrSet(node, "System Device", NWL_NtGetPathFromHandle(hFile), 0);
		CloseHandle(hFile);
	}
	if (pStartOption)
	{
		NWL_NodeAttrSetf(node, "Start Options", NAFLG_FMT_NEED_QUOTE | NAFLG_FMT_STRING, "%ls", pStartOption);
		free(pStartOption);
	}
	DWORD dwBitLocker = 0;
	NWL_GetRegDwordValue(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\BitlockerStatus", "BootStatus", &dwBitLocker);
	NWL_NodeAttrSetBool(node, "BitLocker Boot", dwBitLocker, 0);
}

PNODE NW_UpdateSystem(PNODE node)
{
	PrintUptime(node);
	PrintMemInfo(node);
	return node;
}

PNODE NW_System(VOID)
{
	PNODE node = NWL_NodeAlloc("System", 0);
	if (NWLC->SysInfo)
		NWL_NodeAppendChild(NWLC->NwRoot, node);
	PrintOsVer(node);
	PrintOsInfo(node);
	PrintSysMetrics(node);
	PrintBootInfo(node);
	PrintFwInfo(node);
	PrintTpmInfo(node);
	PrintMemInfo(node);
	return node;
}
