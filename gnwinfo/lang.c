// SPDX-License-Identifier: Unlicense

#include "gnwinfo.h"

#pragma warning(disable:4566)

#define CHS_LANG 2052

typedef struct
{
	LPSTR lpEng;
	LPSTR lpDst;
} GNW_LANG;

static GNW_LANG GNW_LANG_CHS[] =
{
	{"Loading, please wait ...", "�����У����Ժ� ..."},
	{"File", "�ļ�"},
	{"Refresh", "ˢ��"},
	{"Exit", "�˳�"},
	{"Help", "����"},
	{"About", "����"},

	{"Name", "����"},
	{"Attribute", "����"},
	{"Data", "����"},

	{"ACPI Table", "ACPI ��"},
	{"Processor", "������"},
	{"Physical Storage", "����"},
	{"Display Devices", "��ʾ�豸"},
	{"Network Adapter", "����������"},
	{"PCI Devices", "PCI �豸"},
	{"SMBIOS", "SMBIOS"},
	{"Memory SPD", "�ڴ� SPD"},
	{"Operating System", "����ϵͳ"},
	{"USB Devices", "USB �豸"},

	{"Cache", "����"},
	{"Features", "����"},
	{"SGX", "SGX"},
	{"Multiplier", "��Ƶ"},

	{"Min", "��С"},
	{"Max", "���"},
	{"Current", "��ǰ"},
	{"Status", "״̬"},
	{"ID", "ID"},
	{"HWID", "Ӳ�� ID"},
	{"HW Name", "Ӳ������"},
	{"Device", "�豸"},
	{"Type", "����"},
	{"Class", "����"},
	{"Subclass", "����"},
	{"Signature", "ǩ��"},
	{"Length", "����"},
	{"Checksum", "У����"},
	{"Checksum Status", "У����״̬"},
	{"Description", "����"},
	{"Date", "����"},
	{"Version", "�汾"},
	{"Revision", "�޶���"},
	{"Vendor", "��Ӧ��"},
	{"Manufacturer", "������"},
	{"Serial Number", "���к�"},
	{"Path", "·��"},
	{"Size", "��С"},
	{"Capacity", "����"},
	{"Drive Letter", "�̷�"},
	{"Filesystem", "�ļ�ϵͳ"},
	{"Label", "���"},
	{"Free Space", "���ÿռ�"},
	{"Total Space", "�ܿռ�"},
	{"Partition Table", "������"},
	{"Removable", "���ƶ�"},
	{"Table Type", "������"},
	{"Table Length", "����"},
	{"Table Handle", "����"},
	{"Voltage", "��ѹ"},
	{"Prog IF", "��̽ӿ�"},
	{"EDID Version", "EDID �汾"},
	{"Video Input", "��Ƶ����"},
	{"Bits per color", "λ/��ɫ"},
	{"Interface", "����"},
	{"Resolution", "�ֱ���"},
	{"Width", "���"},
	{"Height", "�߶�"},
	{"Refresh Rate (Hz)", "ˢ����"},
	{"Screen Size", "��Ļ�ߴ�"},
	{"Width (mm)", "��� (mm)"},
	{"Height (mm)", "�߶� (mm)"},
	{"Diagonal (in)", "�Խ��� (Ӣ��)"},
	{"Pixel Clock", "����ʱ��Ƶ��"},
};

LPSTR
GNW_GetText(LPCSTR lpEng)
{
	size_t i;
	LPCSTR ret = lpEng;
	if (GNWC.wLang == CHS_LANG)
	{
		for (i = 0; i < sizeof(GNW_LANG_CHS) / sizeof(GNW_LANG_CHS[0]); i++)
		{
			if (strcmp(lpEng, GNW_LANG_CHS[i].lpEng) == 0)
			{
				ret = GNW_LANG_CHS[i].lpDst;
				break;
			}
		}
	}
	return (LPSTR)ret;
}

static VOID
GNW_SetMenuItemText(UINT id)
{
	CHAR cText[MAX_PATH];
	MENUITEMINFOA mii = { 0 };
	mii.cbSize = sizeof(MENUITEMINFOA);
	mii.fMask = MIIM_STRING;
	if (GetMenuStringA(GNWC.hMenu, id, cText, MAX_PATH, MF_BYCOMMAND) == 0)
		return;
	mii.dwTypeData = GNW_GetText(cText);
	SetMenuItemInfoA(GNWC.hMenu, id, FALSE, &mii);
}

VOID
GNW_SetMenuText(VOID)
{
	ModifyMenuA(GNWC.hMenu,
		0, MF_BYPOSITION | MF_STRING | MF_POPUP,
		(UINT_PTR)GetSubMenu(GNWC.hMenu, 0), GNW_GetText("File"));
	ModifyMenuA(GNWC.hMenu,
		1, MF_BYPOSITION | MF_STRING | MF_POPUP,
		(UINT_PTR)GetSubMenu(GNWC.hMenu, 1), GNW_GetText("Help"));
	GNW_SetMenuItemText(IDM_RELOAD);
	GNW_SetMenuItemText(IDM_EXIT);
	GNW_SetMenuItemText(IDM_ABOUT);
}
