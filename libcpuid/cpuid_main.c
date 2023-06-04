/*
 * Copyright 2008  Veselin Georgiev,
 * anrieffNOSPAM @ mgail_DOT.com (convert to gmail)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "libcpuid.h"
#include "libcpuid_internal.h"
#include "recog_intel.h"
#include "recog_amd.h"
#include "asm-bits.h"
#include "libcpuid_util.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Implementation: */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ == 201112L
#define INTERNAL_SCOPE _Thread_local
#elif defined(__GNUC__) // Also works for clang
#define INTERNAL_SCOPE __thread
#else
#define INTERNAL_SCOPE static
#endif

INTERNAL_SCOPE int _libcpuid_errno = ERR_OK;

int cpuid_set_error(cpu_error_t err)
{
	_libcpuid_errno = (int) err;
	return (int) err;
}

static void raw_data_t_constructor(struct cpu_raw_data_t* raw)
{
	memset(raw, 0, sizeof(struct cpu_raw_data_t));
}

static void cpu_id_t_constructor(struct cpu_id_t* id)
{
	memset(id, 0, sizeof(struct cpu_id_t));
	id->l1_data_cache = id->l1_instruction_cache = id->l2_cache = id->l3_cache = id->l4_cache = -1;
	id->l1_assoc = id->l1_data_assoc = id->l1_instruction_assoc = id->l2_assoc = id->l3_assoc = id->l4_assoc = -1;
	id->l1_cacheline = id->l1_data_cacheline = id->l1_instruction_cacheline = id->l2_cacheline = id->l3_cacheline = id->l4_cacheline = -1;
	id->l1_data_instances = id->l1_instruction_instances = id->l2_instances = id->l3_instances = id->l4_instances = -1;
	id->sse_size = -1;
	init_affinity_mask(&id->affinity_mask);
	init_affinity_mask(&id->core_affinity_mask);
	id->purpose = PURPOSE_GENERAL;
}

static void cpu_raw_data_array_t_constructor(struct cpu_raw_data_array_t* raw_array, bool with_affinity)
{
	raw_array->with_affinity = with_affinity;
	raw_array->num_raw = 0;
	raw_array->raw = NULL;
}

static void system_id_t_constructor(struct system_id_t* system)
{
	system->num_cpu_types                  = 0;
	system->cpu_types                      = NULL;
	system->l1_data_total_instances        = -1;
	system->l1_instruction_total_instances = -1;
	system->l2_total_instances             = -1;
	system->l3_total_instances             = -1;
	system->l4_total_instances             = -1;
}

static void apic_info_t_constructor(struct internal_apic_info_t* apic_info, logical_cpu_t logical_cpu)
{
	memset(apic_info, 0, sizeof(struct internal_apic_info_t));
	apic_info->apic_id     = -1;
	apic_info->package_id  = -1;
	apic_info->core_id     = -1;
	apic_info->smt_id      = -1;
	apic_info->logical_cpu = logical_cpu;
}

static void core_instances_t_constructor(struct internal_core_instances_t* data)
{
	data->instances = 0;
	memset(data->htable, 0, sizeof(data->htable));
}

static void cache_instances_t_constructor(struct internal_cache_instances_t* data)
{
	memset(data->instances, 0, sizeof(data->instances));
	memset(data->htable,    0, sizeof(data->htable));
}

static void cpuid_grow_raw_data_array(struct cpu_raw_data_array_t* raw_array, logical_cpu_t n)
{
	logical_cpu_t i;
	struct cpu_raw_data_t *tmp = NULL;

	if ((n <= 0) || (n < raw_array->num_raw)) return;
	tmp = realloc(raw_array->raw, sizeof(struct cpu_raw_data_t) * n);
	if (tmp == NULL) { /* Memory allocation failure */
		cpuid_set_error(ERR_NO_MEM);
		return;
	}

	for (i = raw_array->num_raw; i < n; i++)
		raw_data_t_constructor(&tmp[i]);
	raw_array->num_raw = n;
	raw_array->raw     = tmp;
}

static void cpuid_grow_system_id(struct system_id_t* system, uint8_t n)
{
	uint8_t i;
	struct cpu_id_t *tmp = NULL;

	if ((n <= 0) || (n < system->num_cpu_types)) return;
	tmp = realloc(system->cpu_types, sizeof(struct cpu_id_t) * n);
	if (tmp == NULL) { /* Memory allocation failure */
		cpuid_set_error(ERR_NO_MEM);
		return;
	}

	for (i = system->num_cpu_types; i < n; i++)
		cpu_id_t_constructor(&tmp[i]);
	system->num_cpu_types = n;
	system->cpu_types     = tmp;
}

/* get_total_cpus() system specific code: uses OS routines to determine total number of CPUs */
static int get_total_cpus(void)
{
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return system_info.dwNumberOfProcessors;
}

INTERNAL_SCOPE GROUP_AFFINITY savedGroupAffinity;

bool save_cpu_affinity(void)
{
	HMODULE hMod = GetModuleHandleW(L"kernel32");
	BOOL (WINAPI *NTGetThreadGroupAffinity)(HANDLE hThread, PGROUP_AFFINITY GroupAffinity) = NULL;
	HANDLE thread = GetCurrentThread();
	if (hMod)
		*(FARPROC*)&NTGetThreadGroupAffinity = GetProcAddress(hMod, "NTGetThreadGroupAffinity");
	if (NTGetThreadGroupAffinity)
		return NTGetThreadGroupAffinity(thread, &savedGroupAffinity);

	/* Credits to https://stackoverflow.com/questions/6601862/query-thread-not-process-processor-affinity#6601917 */
	DWORD_PTR threadAffinityMask = 1;
	while (threadAffinityMask) {
		savedGroupAffinity.Mask = SetThreadAffinityMask(thread, threadAffinityMask);
		if(savedGroupAffinity.Mask)
			return SetThreadAffinityMask(thread, savedGroupAffinity.Mask);
		else if (GetLastError() != ERROR_INVALID_PARAMETER)
			return false;
		threadAffinityMask <<= 1; // try next CPU
	}
	return false;
}

bool restore_cpu_affinity(void)
{
	if (!savedGroupAffinity.Mask)
		return false;

	HANDLE thread = GetCurrentThread();
	return SetThreadGroupAffinity(thread, &savedGroupAffinity, NULL);
}

bool set_cpu_affinity(logical_cpu_t logical_cpu)
{
/* Credits to https://github.com/PolygonTek/BlueshiftEngine/blob/fbc374cbc391e1147c744649f405a66a27c35d89/Source/Runtime/Private/Platform/Windows/PlatformWinThread.cpp#L27 */
	int groups = GetActiveProcessorGroupCount();
	int total_processors = 0;
	int group = 0;
	int number = 0;
	int found = 0;
	HANDLE thread = GetCurrentThread();
	GROUP_AFFINITY groupAffinity;

	for (int i = 0; i < groups; i++) {
		int processors = GetActiveProcessorCount(i);
		if (total_processors + processors > logical_cpu) {
			group = i;
			number = logical_cpu - total_processors;
			found = 1;
			break;
		}
		total_processors += processors;
	}
	if (!found) return 0; // logical CPU # too large, does not exist

	memset(&groupAffinity, 0, sizeof(groupAffinity));
	groupAffinity.Group = (WORD) group;
	groupAffinity.Mask = (KAFFINITY) (1ULL << number);
	return SetThreadGroupAffinity(thread, &groupAffinity, NULL);
}

static void load_features_common(struct cpu_raw_data_t* raw, struct cpu_id_t* data)
{
	const struct feature_map_t matchtable_edx1[] = {
		{  0, CPU_FEATURE_FPU },
		{  1, CPU_FEATURE_VME },
		{  2, CPU_FEATURE_DE },
		{  3, CPU_FEATURE_PSE },
		{  4, CPU_FEATURE_TSC },
		{  5, CPU_FEATURE_MSR },
		{  6, CPU_FEATURE_PAE },
		{  7, CPU_FEATURE_MCE },
		{  8, CPU_FEATURE_CX8 },
		{  9, CPU_FEATURE_APIC },
		{ 11, CPU_FEATURE_SEP },
		{ 12, CPU_FEATURE_MTRR },
		{ 13, CPU_FEATURE_PGE },
		{ 14, CPU_FEATURE_MCA },
		{ 15, CPU_FEATURE_CMOV },
		{ 16, CPU_FEATURE_PAT },
		{ 17, CPU_FEATURE_PSE36 },
		{ 19, CPU_FEATURE_CLFLUSH },
		{ 23, CPU_FEATURE_MMX },
		{ 24, CPU_FEATURE_FXSR },
		{ 25, CPU_FEATURE_SSE },
		{ 26, CPU_FEATURE_SSE2 },
		{ 28, CPU_FEATURE_HT },
	};
	const struct feature_map_t matchtable_ecx1[] = {
		{  0, CPU_FEATURE_PNI },
		{  1, CPU_FEATURE_PCLMUL },
		{  3, CPU_FEATURE_MONITOR },
		{  9, CPU_FEATURE_SSSE3 },
		{ 12, CPU_FEATURE_FMA3 },
		{ 13, CPU_FEATURE_CX16 },
		{ 19, CPU_FEATURE_SSE4_1 },
		{ 20, CPU_FEATURE_SSE4_2 },
		{ 22, CPU_FEATURE_MOVBE },
		{ 23, CPU_FEATURE_POPCNT },
		{ 25, CPU_FEATURE_AES },
		{ 26, CPU_FEATURE_XSAVE },
		{ 27, CPU_FEATURE_OSXSAVE },
		{ 28, CPU_FEATURE_AVX },
		{ 29, CPU_FEATURE_F16C },
		{ 30, CPU_FEATURE_RDRAND },
		{ 31, CPU_FEATURE_HYPERVISOR },
	};
	const struct feature_map_t matchtable_ebx7[] = {
		{  3, CPU_FEATURE_BMI1 },
		{  5, CPU_FEATURE_AVX2 },
		{  8, CPU_FEATURE_BMI2 },
		{ 18, CPU_FEATURE_RDSEED },
		{ 19, CPU_FEATURE_ADX },
		{ 29, CPU_FEATURE_SHA_NI },
	};
	const struct feature_map_t matchtable_edx81[] = {
		{ 11, CPU_FEATURE_SYSCALL },
		{ 27, CPU_FEATURE_RDTSCP },
		{ 29, CPU_FEATURE_LM },
	};
	const struct feature_map_t matchtable_ecx81[] = {
		{  0, CPU_FEATURE_LAHF_LM },
		{  5, CPU_FEATURE_ABM },
	};
	const struct feature_map_t matchtable_edx87[] = {
		{  8, CPU_FEATURE_CONSTANT_TSC },
	};
	if (raw->basic_cpuid[0][EAX] >= 1) {
		match_features(matchtable_edx1, COUNT_OF(matchtable_edx1), raw->basic_cpuid[1][EDX], data);
		match_features(matchtable_ecx1, COUNT_OF(matchtable_ecx1), raw->basic_cpuid[1][ECX], data);
	}
	if (raw->basic_cpuid[0][EAX] >= 7) {
		match_features(matchtable_ebx7, COUNT_OF(matchtable_ebx7), raw->basic_cpuid[7][EBX], data);
	}
	if (raw->ext_cpuid[0][EAX] >= 0x80000001) {
		match_features(matchtable_edx81, COUNT_OF(matchtable_edx81), raw->ext_cpuid[1][EDX], data);
		match_features(matchtable_ecx81, COUNT_OF(matchtable_ecx81), raw->ext_cpuid[1][ECX], data);
	}
	if (raw->ext_cpuid[0][EAX] >= 0x80000007) {
		match_features(matchtable_edx87, COUNT_OF(matchtable_edx87), raw->ext_cpuid[7][EDX], data);
	}
	if (data->flags[CPU_FEATURE_SSE]) {
		/* apply guesswork to check if the SSE unit width is 128 bit */
		switch (data->vendor) {
			case VENDOR_AMD:
				data->sse_size = (data->ext_family >= 16 && data->ext_family != 17) ? 128 : 64;
				break;
			case VENDOR_INTEL:
				data->sse_size = (data->family == 6 && data->ext_model >= 15) ? 128 : 64;
				break;
			default:
				break;
		}
		/* leave the CPU_FEATURE_128BIT_SSE_AUTH 0; the advanced per-vendor detection routines
		 * will set it accordingly if they detect the needed bit */
	}
}

static cpu_vendor_t cpuid_vendor_identify(const uint32_t *raw_vendor, char *vendor_str)
{
	int i;
	cpu_vendor_t vendor = VENDOR_UNKNOWN;
	const struct { cpu_vendor_t vendor; char match[16]; }
	matchtable[] = {
		/* source: http://www.sandpile.org/ia32/cpuid.htm */
		{ VENDOR_INTEL		, "GenuineIntel" },
		{ VENDOR_AMD		, "AuthenticAMD" },
		{ VENDOR_AMD		, "AMDisbetter!" }, // AMD K5
		{ VENDOR_CYRIX		, "CyrixInstead" },
		{ VENDOR_NEXGEN		, "NexGenDriven" },
		{ VENDOR_TRANSMETA	, "GenuineTMx86" },
		{ VENDOR_TRANSMETA	, "TransmetaCPU" },
		{ VENDOR_UMC		, "UMC UMC UMC " },
		{ VENDOR_CENTAUR	, "CentaurHauls" },
		{ VENDOR_RISE		, "RiseRiseRise" },
		{ VENDOR_SIS		, "SiS SiS SiS " },
		{ VENDOR_NSC		, "Geode by NSC" },
		{ VENDOR_HYGON		, "HygonGenuine" },
		{ VENDOR_VORTEX86	, "Vortex86 SoC" },
		{ VENDOR_VIA		, "VIA VIA VIA " },
		{ VENDOR_ZHAOXIN	, "  Shanghai  " },
	};

	memcpy(vendor_str + 0, &raw_vendor[1], 4);
	memcpy(vendor_str + 4, &raw_vendor[3], 4);
	memcpy(vendor_str + 8, &raw_vendor[2], 4);
	vendor_str[12] = 0;

	/* Determine vendor: */
	for (i = 0; i < sizeof(matchtable) / sizeof(matchtable[0]); i++)
		if (!strcmp(vendor_str, matchtable[i].match)) {
			vendor = matchtable[i].vendor;
			break;
		}
	return vendor;
}

static hypervisor_vendor_t cpuid_get_hypervisor(struct cpu_id_t* data)
{
	int i;
	uint32_t hypervisor_fn40000000h[NUM_REGS];
	const struct { hypervisor_vendor_t hypervisor; char match[16]; }
	matchtable[] = {
		/* source: https://github.com/a0rtega/pafish/blob/master/pafish/cpu.c */
		{ HYPERVISOR_ACRN       , "ACRNACRNACRN"    },
		{ HYPERVISOR_BHYVE      , "bhyve bhyve\0"   },
		{ HYPERVISOR_HYPERV     , "Microsoft Hv"    },
		{ HYPERVISOR_KVM        , "KVMKVMKVM\0\0\0" },
		{ HYPERVISOR_KVM        , "Linux KVM Hv"    },
		{ HYPERVISOR_PARALLELS  , "prl hyperv\0\0"  },
		{ HYPERVISOR_PARALLELS  , "lrpepyh vr\0\0"  },
		{ HYPERVISOR_QEMU       , "TCGTCGTCGTCG"    },
		{ HYPERVISOR_QNX        , "QNXQVMBSQG\0\0"  },
		{ HYPERVISOR_VIRTUALBOX , "VBoxVBoxVBox"    },
		{ HYPERVISOR_VMWARE     , "VMwareVMware"    },
		{ HYPERVISOR_XEN        , "XenVMMXenVMM"    },
	};

	memset(data->hypervisor_str, 0, VENDOR_STR_MAX);

	/* Intel and AMD CPUs have reserved bit 31 of ECX of CPUID leaf 0x1 as the hypervisor present bit
	Source: https://kb.vmware.com/s/article/1009458 */
	switch (data->vendor) {
	case VENDOR_AMD:
	case VENDOR_INTEL:
		break;
	default:
		return HYPERVISOR_UNKNOWN;
	}
	if (!data->flags[CPU_FEATURE_HYPERVISOR])
		return HYPERVISOR_NONE;

	/* Intel and AMD have also reserved CPUID leaves 0x40000000 - 0x400000FF for software use.
	Hypervisors can use these leaves to provide an interface to pass information
	from the hypervisor to the guest operating system running inside a virtual machine.
	The hypervisor bit indicates the presence of a hypervisor
	and that it is safe to test these additional software leaves. */
	memset(hypervisor_fn40000000h, 0, sizeof(hypervisor_fn40000000h));
	hypervisor_fn40000000h[EAX] = 0x40000000;
	cpu_exec_cpuid_ext(hypervisor_fn40000000h);

	/* Copy the hypervisor CPUID information leaf */
	memcpy(data->hypervisor_str + 0, &hypervisor_fn40000000h[1], 4);
	memcpy(data->hypervisor_str + 4, &hypervisor_fn40000000h[2], 4);
	memcpy(data->hypervisor_str + 8, &hypervisor_fn40000000h[3], 4);
	data->hypervisor_str[12] = '\0';

	/* Determine hypervisor */
	for (i = 0; i < sizeof(matchtable) / sizeof(matchtable[0]); i++)
		if (!strcmp(data->hypervisor_str, matchtable[i].match))
			return matchtable[i].hypervisor;
	return HYPERVISOR_UNKNOWN;
}

static int cpuid_basic_identify(struct cpu_raw_data_t* raw, struct cpu_id_t* data)
{
	int i, j, basic, xmodel, xfamily, ext;
	char brandstr[64] = {0};
	data->vendor = cpuid_vendor_identify(raw->basic_cpuid[0], data->vendor_str);

	if (data->vendor == VENDOR_UNKNOWN)
		return cpuid_set_error(ERR_CPU_UNKN);
	data->architecture = ARCHITECTURE_X86;
	basic = raw->basic_cpuid[0][EAX];
	if (basic >= 1) {
		data->family = (raw->basic_cpuid[1][EAX] >> 8) & 0xf;
		data->model = (raw->basic_cpuid[1][EAX] >> 4) & 0xf;
		data->stepping = raw->basic_cpuid[1][EAX] & 0xf;
		xmodel = (raw->basic_cpuid[1][EAX] >> 16) & 0xf;
		xfamily = (raw->basic_cpuid[1][EAX] >> 20) & 0xff;
		if (data->vendor == VENDOR_AMD && data->family < 0xf)
			data->ext_family = data->family;
		else
			data->ext_family = data->family + xfamily;
		data->ext_model = data->model + (xmodel << 4);
	}
	ext = raw->ext_cpuid[0][EAX] - 0x80000000;

	/* obtain the brand string, if present: */
	if (ext >= 4) {
		for (i = 0; i < 3; i++)
			for (j = 0; j < 4; j++)
				memcpy(brandstr + 16ULL * i + 4ULL * j,
				       &raw->ext_cpuid[2 + i][j], 4);
		brandstr[48] = 0;
		i = 0;
		while (brandstr[i] == ' ') i++;
		strncpy_s(data->brand_str, BRAND_STR_MAX, brandstr + i, sizeof(data->brand_str));
		data->brand_str[48] = 0;
	}
	load_features_common(raw, data);
	data->total_logical_cpus = get_total_cpus();
	data->hypervisor_vendor = cpuid_get_hypervisor(data);
	return cpuid_set_error(ERR_OK);
}

static bool cpu_ident_apic_id(logical_cpu_t logical_cpu, struct cpu_raw_data_t* raw, struct internal_apic_info_t* apic_info)
{
	bool is_apic_id_supported = false;
	uint8_t subleaf;
	uint8_t level_type = 0;
	uint8_t mask_core_shift = 0;
	uint32_t mask_smt_shift, core_plus_mask_width, package_mask, core_mask, smt_mask = 0;
	cpu_vendor_t vendor = VENDOR_UNKNOWN;
	char vendor_str[VENDOR_STR_MAX];

	apic_info_t_constructor(apic_info, logical_cpu);

	/* Only AMD and Intel x86 CPUs support Extended Processor Topology Eumeration */
	vendor = cpuid_vendor_identify(raw->basic_cpuid[0], vendor_str);
	switch (vendor) {
		case VENDOR_INTEL:
		case VENDOR_AMD:
			is_apic_id_supported = true;
			break;
		case VENDOR_UNKNOWN:
			cpuid_set_error(ERR_CPU_UNKN);
			/* Fall through */
		default:
			is_apic_id_supported = false;
			break;
	}

	/* Documentation: Intel® 64 and IA-32 Architectures Software Developer’s Manual
	   Combined Volumes: 1, 2A, 2B, 2C, 2D, 3A, 3B, 3C, 3D and 4
	   https://cdrdv2.intel.com/v1/dl/getContent/671200
	   Examples 8-20 and 8-22 are implemented below.
	*/

	/* Check if leaf 0Bh is supported and if number of logical processors at this level type is greater than 0 */
	if (!is_apic_id_supported || (raw->basic_cpuid[0][EAX] < 11) || (EXTRACTS_BITS(raw->basic_cpuid[11][EBX], 15, 0) == 0)) {
		// Warning: APIC ID are not supported, core count can be wrong if SMT is disabled and cache instances count will not be available.
		return false;
	}

	/* Derive core mask offsets */
	for (subleaf = 0; (raw->intel_fn11[subleaf][EAX] != 0x0) && (raw->intel_fn11[subleaf][EBX] != 0x0) && (subleaf < MAX_INTELFN11_LEVEL); subleaf++)
		mask_core_shift = EXTRACTS_BITS(raw->intel_fn11[subleaf][EAX], 4, 0);

	/* Find mask and ID for SMT and cores */
	for (subleaf = 0; (raw->intel_fn11[subleaf][EAX] != 0x0) && (raw->intel_fn11[subleaf][EBX] != 0x0) && (subleaf < MAX_INTELFN11_LEVEL); subleaf++) {
		level_type         = EXTRACTS_BITS(raw->intel_fn11[subleaf][ECX], 15, 8);
		apic_info->apic_id = raw->intel_fn11[subleaf][EDX];
		switch (level_type) {
			case 0x01:
				mask_smt_shift    = EXTRACTS_BITS(raw->intel_fn11[subleaf][EAX], 4, 0);
				smt_mask          = ~(((uint32_t) -1) << mask_smt_shift);
				apic_info->smt_id = apic_info->apic_id & smt_mask;
				break;
			case 0x02:
				core_plus_mask_width = ~(((uint32_t) -1) << mask_core_shift);
				core_mask            = core_plus_mask_width ^ smt_mask;
				apic_info->core_id   = apic_info->apic_id & core_mask;
				break;
			default:
				break;
		}
	}

	/* Find mask and ID for packages */
	package_mask          = ((uint32_t) -1) << mask_core_shift;
	apic_info->package_id = apic_info->apic_id & package_mask;

	return (level_type > 0);
}


/* Interface: */

int cpuid_get_total_cpus(void)
{
	return get_total_cpus();
}

int cpuid_present(void)
{
	return cpuid_exists_by_eflags();
}

void cpu_exec_cpuid(uint32_t eax, uint32_t* regs)
{
	regs[0] = eax;
	regs[1] = regs[2] = regs[3] = 0;
	exec_cpuid(regs);
}

void cpu_exec_cpuid_ext(uint32_t* regs)
{
	exec_cpuid(regs);
}

int cpuid_get_raw_data(struct cpu_raw_data_t* data)
{
	unsigned i;
	if (!cpuid_present())
		return cpuid_set_error(ERR_NO_CPUID);
	for (i = 0; i < 32; i++)
		cpu_exec_cpuid(i, data->basic_cpuid[i]);
	for (i = 0; i < 32; i++)
		cpu_exec_cpuid(0x80000000 + i, data->ext_cpuid[i]);
	for (i = 0; i < MAX_INTELFN4_LEVEL; i++) {
		memset(data->intel_fn4[i], 0, sizeof(data->intel_fn4[i]));
		data->intel_fn4[i][EAX] = 4;
		data->intel_fn4[i][ECX] = i;
		cpu_exec_cpuid_ext(data->intel_fn4[i]);
	}
	for (i = 0; i < MAX_INTELFN11_LEVEL; i++) {
		memset(data->intel_fn11[i], 0, sizeof(data->intel_fn11[i]));
		data->intel_fn11[i][EAX] = 11;
		data->intel_fn11[i][ECX] = i;
		cpu_exec_cpuid_ext(data->intel_fn11[i]);
	}
	for (i = 0; i < MAX_INTELFN12H_LEVEL; i++) {
		memset(data->intel_fn12h[i], 0, sizeof(data->intel_fn12h[i]));
		data->intel_fn12h[i][EAX] = 0x12;
		data->intel_fn12h[i][ECX] = i;
		cpu_exec_cpuid_ext(data->intel_fn12h[i]);
	}
	for (i = 0; i < MAX_INTELFN14H_LEVEL; i++) {
		memset(data->intel_fn14h[i], 0, sizeof(data->intel_fn14h[i]));
		data->intel_fn14h[i][EAX] = 0x14;
		data->intel_fn14h[i][ECX] = i;
		cpu_exec_cpuid_ext(data->intel_fn14h[i]);
	}
	for (i = 0; i < MAX_AMDFN8000001DH_LEVEL; i++) {
		memset(data->amd_fn8000001dh[i], 0, sizeof(data->amd_fn8000001dh[i]));
		data->amd_fn8000001dh[i][EAX] = 0x8000001d;
		data->amd_fn8000001dh[i][ECX] = i;
		cpu_exec_cpuid_ext(data->amd_fn8000001dh[i]);
	}
	return cpuid_set_error(ERR_OK);
}

int cpuid_get_all_raw_data(struct cpu_raw_data_array_t* data)
{
	int cur_error = cpuid_set_error(ERR_OK);
	int ret_error = cpuid_set_error(ERR_OK);
	logical_cpu_t logical_cpu = 0;
	struct cpu_raw_data_t* raw_ptr = NULL;

	if (data == NULL)
		return cpuid_set_error(ERR_HANDLE);

	bool affinity_saved = save_cpu_affinity();

	cpu_raw_data_array_t_constructor(data, true);
	while (set_cpu_affinity(logical_cpu) || logical_cpu == 0) {
		cpuid_grow_raw_data_array(data, logical_cpu + 1);
		raw_ptr = &data->raw[logical_cpu];
		cur_error = cpuid_get_raw_data(raw_ptr);
		if (ret_error == ERR_OK)
			ret_error = cur_error;
		logical_cpu++;
	}

	if (affinity_saved)
		restore_cpu_affinity();

	return ret_error;
}

int cpu_ident_internal(struct cpu_raw_data_t* raw, struct cpu_id_t* data, struct internal_id_info_t* internal)
{
	int r;
	struct cpu_raw_data_t myraw;
	if (!raw) {
		if ((r = cpuid_get_raw_data(&myraw)) < 0)
			return cpuid_set_error(r);
		raw = &myraw;
	}
	cpu_id_t_constructor(data);
	memset(internal->cache_mask, 0, sizeof(internal->cache_mask));
	if ((r = cpuid_basic_identify(raw, data)) < 0)
		return cpuid_set_error(r);
	switch (data->vendor) {
		case VENDOR_INTEL:
			r = cpuid_identify_intel(raw, data, internal);
			break;
		case VENDOR_AMD:
		case VENDOR_HYGON:
			r = cpuid_identify_amd(raw, data, internal);
			break;
		default:
			break;
	}
	/* Backward compatibility */
	/* - Deprecated since v0.5.0 */
	data->l1_assoc     = data->l1_data_assoc;
	data->l1_cacheline = data->l1_data_cacheline;
	return cpuid_set_error(r);
}

static cpu_purpose_t cpu_ident_purpose(struct cpu_raw_data_t* raw)
{
	cpu_vendor_t vendor = VENDOR_UNKNOWN;
	cpu_purpose_t purpose = PURPOSE_GENERAL;
	char vendor_str[VENDOR_STR_MAX];

	vendor = cpuid_vendor_identify(raw->basic_cpuid[0], vendor_str);
	if (vendor == VENDOR_UNKNOWN) {
		cpuid_set_error(ERR_CPU_UNKN);
		return purpose;
	}

	switch (vendor) {
		case VENDOR_AMD:
			purpose = cpuid_identify_purpose_amd(raw);
			break;
		case VENDOR_INTEL:
			purpose = cpuid_identify_purpose_intel(raw);
			break;
		default:
			purpose = PURPOSE_GENERAL;
			break;
	}

	return purpose;
}

int cpu_identify(struct cpu_raw_data_t* raw, struct cpu_id_t* data)
{
	int r;
	struct internal_id_info_t throwaway;
	r = cpu_ident_internal(raw, data, &throwaway);
	return r;
}

static void update_core_instances(struct internal_core_instances_t* cores,
	struct internal_apic_info_t* apic_info,
	logical_cpu_t logical_cpu, cpu_affinity_mask_t* mask)
{
	uint32_t core_id_index = 0;

	core_id_index = apic_info->core_id % CORES_HTABLE_SIZE;
	if ((cores->htable[core_id_index].core_id == 0) || (cores->htable[core_id_index].core_id == apic_info->core_id)) {
		if (cores->htable[core_id_index].num_logical_cpu == 0)
			cores->instances++;
		else
			clear_affinity_mask_bit(logical_cpu, mask);
		cores->htable[core_id_index].core_id = apic_info->core_id;
		cores->htable[core_id_index].num_logical_cpu++;
	}
	else {
		// Warning: update_core_instances: collision at core_id_index
	}
}

static void update_cache_instances(struct internal_cache_instances_t* caches,
                                   struct internal_apic_info_t* apic_info,
                                   struct internal_id_info_t* id_info)
{
	uint32_t cache_id_index = 0;
	cache_type_t level;

	for (level = 0; level < NUM_CACHE_TYPES; level++) {
		if (id_info->cache_mask[level] == 0x00000000) {
			apic_info->cache_id[level] = -1;
			continue;
		}
		apic_info->cache_id[level] = apic_info->apic_id & id_info->cache_mask[level];
		cache_id_index             = apic_info->cache_id[level] % CACHES_HTABLE_SIZE;
		if ((caches->htable[level][cache_id_index].cache_id == 0) || (caches->htable[level][cache_id_index].cache_id == apic_info->cache_id[level])) {
			if (caches->htable[level][cache_id_index].num_logical_cpu == 0)
				caches->instances[level]++;
			caches->htable[level][cache_id_index].cache_id = apic_info->cache_id[level];
			caches->htable[level][cache_id_index].num_logical_cpu++;
		}
		else {
			// Warning: collision at index cache_id_index
		}
	}
}

int cpu_identify_all(struct cpu_raw_data_array_t* raw_array, struct system_id_t* system)
{
	int cur_error = cpuid_set_error(ERR_OK);
	int ret_error = cpuid_set_error(ERR_OK);
	double smt_divisor;
	bool is_new_cpu_type;
	bool is_last_item;
	bool is_smt_supported;
	bool is_apic_supported = true;
	uint8_t cpu_type_index = 0;
	int32_t num_logical_cpus = 0;
	int32_t cur_package_id = 0;
	int32_t prev_package_id = 0;
	logical_cpu_t logical_cpu = 0;
	cpu_purpose_t purpose;
	cpu_affinity_mask_t* affinity_mask = malloc(sizeof(*affinity_mask));
	cpu_affinity_mask_t *core_affinity_mask = malloc(sizeof(*core_affinity_mask));
	struct internal_id_info_t id_info;
	struct internal_apic_info_t apic_info;
	struct internal_core_instances_t* cores_type = malloc(sizeof(*cores_type));
	struct internal_cache_instances_t* caches_type = malloc(sizeof(*caches_type));
	struct internal_cache_instances_t* caches_all = malloc(sizeof(*caches_all));

	if (!system || !raw_array
		|| !cores_type || !caches_type || !caches_all || !affinity_mask || !core_affinity_mask)
	{
		if (affinity_mask)
			free(affinity_mask);
		if (core_affinity_mask)
			free(core_affinity_mask);
		if (cores_type)
			free(cores_type);
		if (caches_type)
			free(caches_type);
		if (caches_all)
			free(caches_all);
		return cpuid_set_error(ERR_HANDLE);
	}

	system_id_t_constructor(system);
	core_instances_t_constructor(cores_type);
	cache_instances_t_constructor(caches_type);
	cache_instances_t_constructor(caches_all);
	if (raw_array->with_affinity)
	{
		init_affinity_mask(affinity_mask);
		init_affinity_mask(core_affinity_mask);
	}

	/* Iterate over all raw */
	for (logical_cpu = 0; logical_cpu < raw_array->num_raw; logical_cpu++) {
		is_new_cpu_type = false;
		is_last_item    = (logical_cpu + 1 >= raw_array->num_raw);
		/* Get CPU purpose and APIC ID
		   For hybrid CPUs, the purpose may be different than the previous iteration (e.g. from P-cores to E-cores)
		   APIC ID are unique for each logical CPU cores.
		*/
		purpose = cpu_ident_purpose(&raw_array->raw[logical_cpu]);
		if (raw_array->with_affinity && is_apic_supported) {
			is_apic_supported = cpu_ident_apic_id(logical_cpu, &raw_array->raw[logical_cpu], &apic_info);
			if (is_apic_supported)
				cur_package_id = apic_info.package_id;
		}

		/* Put data to system->cpu_types on the first iteration or when purpose is different than previous core */
		if ((system->num_cpu_types == 0) || (purpose != system->cpu_types[system->num_cpu_types - 1].purpose) || (cur_package_id != prev_package_id)) {
			is_new_cpu_type = true;
			cpu_type_index  = system->num_cpu_types;
			cpuid_grow_system_id(system, system->num_cpu_types + 1);
			cur_error = cpu_ident_internal(&raw_array->raw[logical_cpu], &system->cpu_types[cpu_type_index], &id_info);
			if (ret_error == ERR_OK)
				ret_error = cur_error;
		}
		/* Increment counters only for current purpose */
		if (raw_array->with_affinity && ((logical_cpu == 0) || !is_new_cpu_type)) {
			set_affinity_mask_bit(logical_cpu, affinity_mask);
			set_affinity_mask_bit(logical_cpu, core_affinity_mask);
			num_logical_cpus++;
			if (is_apic_supported) {
				update_core_instances(cores_type, &apic_info, logical_cpu, core_affinity_mask);
				update_cache_instances(caches_type, &apic_info, &id_info);
				update_cache_instances(caches_all, &apic_info, &id_info);
			}
		}

		/* Update logical CPU counters, physical CPU counters and cache instance in system->cpu_types
		   Note: we need to differenciate two events:
		     - is_new_cpu_type (e.g. purpose was 'efficiency' during previous loop, and now purpose is 'performance')
		     - is_last_item (i.e. this is the last iteration in raw_array->num_raw)
		   In some cases, both events can occur during the same iteration, thus we have to update counters twice for the same logical_cpu.
		   This occurs with single-core CPU type. For instance, Pentacore Lakefield CPU consists of:
		     - 1 "big" Sunny Cove core
		     - 4 "little" Tremont cores
		   On the last iteration, there is no need to reset values for the next purpose.
		*/
		if (raw_array->with_affinity && (is_last_item || (is_new_cpu_type && (system->num_cpu_types > 1)))) {
			enum event_t {
				EVENT_NEW_CPU_TYPE = 0,
				EVENT_LAST_ITEM    = 1
			};
			const enum event_t first_event = is_new_cpu_type && (system->num_cpu_types > 1) ? EVENT_NEW_CPU_TYPE : EVENT_LAST_ITEM;
			const enum event_t last_event  = is_last_item                                   ? EVENT_LAST_ITEM    : EVENT_NEW_CPU_TYPE;
			for (enum event_t event = first_event; event <= last_event; event++) {
				switch (event) {
					case EVENT_NEW_CPU_TYPE: cpu_type_index = system->num_cpu_types - 2; break;
					case EVENT_LAST_ITEM:    cpu_type_index = system->num_cpu_types - 1; break;
					default: ret_error = cpuid_set_error(ERR_NOT_IMP); goto out;
				}
				copy_affinity_mask(&system->cpu_types[cpu_type_index].affinity_mask, affinity_mask);
				copy_affinity_mask(&system->cpu_types[cpu_type_index].core_affinity_mask, core_affinity_mask);
				if (event != EVENT_LAST_ITEM) {
					init_affinity_mask(affinity_mask);
					init_affinity_mask(core_affinity_mask);
					set_affinity_mask_bit(logical_cpu, affinity_mask);
					set_affinity_mask_bit(logical_cpu, core_affinity_mask);
				}
				if (is_apic_supported) {
					system->cpu_types[cpu_type_index].num_cores                = cores_type->instances;
					system->cpu_types[cpu_type_index].l1_instruction_instances = caches_type->instances[L1I];
					system->cpu_types[cpu_type_index].l1_data_instances        = caches_type->instances[L1D];
					system->cpu_types[cpu_type_index].l2_instances             = caches_type->instances[L2];
					system->cpu_types[cpu_type_index].l3_instances             = caches_type->instances[L3];
					system->cpu_types[cpu_type_index].l4_instances             = caches_type->instances[L4];
					if (event != EVENT_LAST_ITEM) {
						core_instances_t_constructor(cores_type);
						cache_instances_t_constructor(caches_type);
						update_core_instances(cores_type, &apic_info, logical_cpu, core_affinity_mask);
						update_cache_instances(caches_type, &apic_info, &id_info);
						update_cache_instances(caches_all, &apic_info, &id_info);
					}
				}
				else {
					/* Note: if SMT is disabled by BIOS, smt_divisor will no reflect the current state properly */
					is_smt_supported = system->cpu_types[cpu_type_index].num_cores > 0 ? (system->cpu_types[cpu_type_index].num_logical_cpus % system->cpu_types[cpu_type_index].num_cores) == 0 : false;
					smt_divisor      = is_smt_supported ? system->cpu_types[cpu_type_index].num_logical_cpus / system->cpu_types[cpu_type_index].num_cores : 1.0;
					system->cpu_types[cpu_type_index].num_cores = (int32_t) (num_logical_cpus / smt_divisor);
				}
				/* Save current values in system->cpu_types[cpu_type_index] and reset values for the next purpose */
				system->cpu_types[cpu_type_index].num_logical_cpus = num_logical_cpus;
				num_logical_cpus = 1;
			}
		}
		prev_package_id = cur_package_id;
	}

	/* Update the grand total of cache instances */
	if (is_apic_supported) {
		system->l1_instruction_total_instances = caches_all->instances[L1I];
		system->l1_data_total_instances        = caches_all->instances[L1D];
		system->l2_total_instances             = caches_all->instances[L2];
		system->l3_total_instances             = caches_all->instances[L3];
		system->l4_total_instances             = caches_all->instances[L4];
	}

	/* Update the total_logical_cpus value for each purpose */
	for (cpu_type_index = 0; cpu_type_index < system->num_cpu_types; cpu_type_index++)
		system->cpu_types[cpu_type_index].total_logical_cpus = logical_cpu;

out:
	free(affinity_mask);
	free(core_affinity_mask);
	free(cores_type);
	free(caches_type);
	free(caches_all);
	return ret_error;
}

int cpu_request_core_type(cpu_purpose_t purpose, struct cpu_raw_data_array_t* raw_array, struct cpu_id_t* data)
{
	int error;
	logical_cpu_t logical_cpu = 0;
	struct cpu_raw_data_array_t my_raw_array;
	struct internal_id_info_t throwaway;

	if (!raw_array) {
		if ((error = cpuid_get_all_raw_data(&my_raw_array)) < 0)
			return cpuid_set_error(error);
		raw_array = &my_raw_array;
	}

	for (logical_cpu = 0; logical_cpu < raw_array->num_raw; logical_cpu++) {
		if (cpu_ident_purpose(&raw_array->raw[logical_cpu]) == purpose) {
			cpu_ident_internal(&raw_array->raw[logical_cpu], data, &throwaway);
			return cpuid_set_error(ERR_OK);
		}
	}

	return cpuid_set_error(ERR_NOT_FOUND);
}

const char* cpu_architecture_str(cpu_architecture_t architecture)
{
	const struct { cpu_architecture_t architecture; const char* name; }
	matchtable[] = {
		{ ARCHITECTURE_UNKNOWN, "unknown" },
		{ ARCHITECTURE_X86,     "x86"     },
		{ ARCHITECTURE_ARM,     "ARM"     },
	};
	unsigned i, n = COUNT_OF(matchtable);
	if (n != NUM_CPU_ARCHITECTURES + 1) {
		// Warning: incomplete library
		// architecture matchtable size differs from the actual number of architectures.
	}
	for (i = 0; i < n; i++)
		if (matchtable[i].architecture == architecture)
			return matchtable[i].name;
	return "";
}

const char* cpu_purpose_str(cpu_purpose_t purpose)
{
	const struct { cpu_purpose_t purpose; const char* name; }
	matchtable[] = {
		{ PURPOSE_GENERAL,     "general"     },
		{ PURPOSE_PERFORMANCE, "performance" },
		{ PURPOSE_EFFICIENCY,  "efficiency"  },
	};
	unsigned i, n = COUNT_OF(matchtable);
	if (n != NUM_CPU_PURPOSES) {
		// Warning: incomplete library
		// purpose matchtable size differs from the actual number of purposes.
	}
	for (i = 0; i < n; i++)
		if (matchtable[i].purpose == purpose)
			return matchtable[i].name;
	return "";
}

char* affinity_mask_str_r(cpu_affinity_mask_t* affinity_mask, char* buffer, uint32_t buffer_len)
{
	logical_cpu_t mask_index = __MASK_SETSIZE - 1;
	logical_cpu_t str_index = 0;
	bool do_print = false;

	while (((uint32_t) str_index) + 1 < buffer_len) {
		if (do_print || (mask_index < 4) || (affinity_mask->__bits[mask_index] != 0x00)) {
			snprintf(&buffer[str_index], 3, "%02X", affinity_mask->__bits[mask_index]);
			do_print = true;
			str_index += 2;
		}
		/* mask_index in unsigned, so we cannot decrement it beyond 0 */
		if (mask_index == 0)
			break;
		mask_index--;
	}
	buffer[str_index] = '\0';

	return buffer;
}

char* affinity_mask_str(cpu_affinity_mask_t* affinity_mask)
{
	static char buffer[__MASK_SETSIZE + 1] = "";
	return affinity_mask_str_r(affinity_mask, buffer, __MASK_SETSIZE + 1);
}

const char* cpu_feature_str(cpu_feature_t feature)
{
	const struct { cpu_feature_t feature; const char* name; }
	matchtable[] = {
		{ CPU_FEATURE_FPU, "fpu" },
		{ CPU_FEATURE_VME, "vme" },
		{ CPU_FEATURE_DE, "de" },
		{ CPU_FEATURE_PSE, "pse" },
		{ CPU_FEATURE_TSC, "tsc" },
		{ CPU_FEATURE_MSR, "msr" },
		{ CPU_FEATURE_PAE, "pae" },
		{ CPU_FEATURE_MCE, "mce" },
		{ CPU_FEATURE_CX8, "cx8" },
		{ CPU_FEATURE_APIC, "apic" },
		{ CPU_FEATURE_MTRR, "mtrr" },
		{ CPU_FEATURE_SEP, "sep" },
		{ CPU_FEATURE_PGE, "pge" },
		{ CPU_FEATURE_MCA, "mca" },
		{ CPU_FEATURE_CMOV, "cmov" },
		{ CPU_FEATURE_PAT, "pat" },
		{ CPU_FEATURE_PSE36, "pse36" },
		{ CPU_FEATURE_PN, "pn" },
		{ CPU_FEATURE_CLFLUSH, "clflush" },
		{ CPU_FEATURE_DTS, "dts" },
		{ CPU_FEATURE_ACPI, "acpi" },
		{ CPU_FEATURE_MMX, "mmx" },
		{ CPU_FEATURE_FXSR, "fxsr" },
		{ CPU_FEATURE_SSE, "sse" },
		{ CPU_FEATURE_SSE2, "sse2" },
		{ CPU_FEATURE_SS, "ss" },
		{ CPU_FEATURE_HT, "ht" },
		{ CPU_FEATURE_TM, "tm" },
		{ CPU_FEATURE_IA64, "ia64" },
		{ CPU_FEATURE_PBE, "pbe" },
		{ CPU_FEATURE_PNI, "pni" },
		{ CPU_FEATURE_PCLMUL, "pclmul" },
		{ CPU_FEATURE_DTS64, "dts64" },
		{ CPU_FEATURE_MONITOR, "monitor" },
		{ CPU_FEATURE_DS_CPL, "ds_cpl" },
		{ CPU_FEATURE_VMX, "vmx" },
		{ CPU_FEATURE_SMX, "smx" },
		{ CPU_FEATURE_EST, "est" },
		{ CPU_FEATURE_TM2, "tm2" },
		{ CPU_FEATURE_SSSE3, "ssse3" },
		{ CPU_FEATURE_CID, "cid" },
		{ CPU_FEATURE_CX16, "cx16" },
		{ CPU_FEATURE_XTPR, "xtpr" },
		{ CPU_FEATURE_PDCM, "pdcm" },
		{ CPU_FEATURE_DCA, "dca" },
		{ CPU_FEATURE_SSE4_1, "sse4_1" },
		{ CPU_FEATURE_SSE4_2, "sse4_2" },
		{ CPU_FEATURE_SYSCALL, "syscall" },
		{ CPU_FEATURE_XD, "xd" },
		{ CPU_FEATURE_X2APIC, "x2apic"},
		{ CPU_FEATURE_MOVBE, "movbe" },
		{ CPU_FEATURE_POPCNT, "popcnt" },
		{ CPU_FEATURE_AES, "aes" },
		{ CPU_FEATURE_XSAVE, "xsave" },
		{ CPU_FEATURE_OSXSAVE, "osxsave" },
		{ CPU_FEATURE_AVX, "avx" },
		{ CPU_FEATURE_MMXEXT, "mmxext" },
		{ CPU_FEATURE_3DNOW, "3dnow" },
		{ CPU_FEATURE_3DNOWEXT, "3dnowext" },
		{ CPU_FEATURE_NX, "nx" },
		{ CPU_FEATURE_FXSR_OPT, "fxsr_opt" },
		{ CPU_FEATURE_RDTSCP, "rdtscp" },
		{ CPU_FEATURE_LM, "lm" },
		{ CPU_FEATURE_LAHF_LM, "lahf_lm" },
		{ CPU_FEATURE_CMP_LEGACY, "cmp_legacy" },
		{ CPU_FEATURE_SVM, "svm" },
		{ CPU_FEATURE_SSE4A, "sse4a" },
		{ CPU_FEATURE_MISALIGNSSE, "misalignsse" },
		{ CPU_FEATURE_ABM, "abm" },
		{ CPU_FEATURE_3DNOWPREFETCH, "3dnowprefetch" },
		{ CPU_FEATURE_OSVW, "osvw" },
		{ CPU_FEATURE_IBS, "ibs" },
		{ CPU_FEATURE_SSE5, "sse5" },
		{ CPU_FEATURE_SKINIT, "skinit" },
		{ CPU_FEATURE_WDT, "wdt" },
		{ CPU_FEATURE_TS, "ts" },
		{ CPU_FEATURE_FID, "fid" },
		{ CPU_FEATURE_VID, "vid" },
		{ CPU_FEATURE_TTP, "ttp" },
		{ CPU_FEATURE_TM_AMD, "tm_amd" },
		{ CPU_FEATURE_STC, "stc" },
		{ CPU_FEATURE_100MHZSTEPS, "100mhzsteps" },
		{ CPU_FEATURE_HWPSTATE, "hwpstate" },
		{ CPU_FEATURE_CONSTANT_TSC, "constant_tsc" },
		{ CPU_FEATURE_XOP, "xop" },
		{ CPU_FEATURE_FMA3, "fma3" },
		{ CPU_FEATURE_FMA4, "fma4" },
		{ CPU_FEATURE_TBM, "tbm" },
		{ CPU_FEATURE_F16C, "f16c" },
		{ CPU_FEATURE_RDRAND, "rdrand" },
		{ CPU_FEATURE_CPB, "cpb" },
		{ CPU_FEATURE_APERFMPERF, "aperfmperf" },
		{ CPU_FEATURE_PFI, "pfi" },
		{ CPU_FEATURE_PA, "pa" },
		{ CPU_FEATURE_AVX2, "avx2" },
		{ CPU_FEATURE_BMI1, "bmi1" },
		{ CPU_FEATURE_BMI2, "bmi2" },
		{ CPU_FEATURE_HLE, "hle" },
		{ CPU_FEATURE_RTM, "rtm" },
		{ CPU_FEATURE_AVX512F, "avx512f" },
		{ CPU_FEATURE_AVX512DQ, "avx512dq" },
		{ CPU_FEATURE_AVX512PF, "avx512pf" },
		{ CPU_FEATURE_AVX512ER, "avx512er" },
		{ CPU_FEATURE_AVX512CD, "avx512cd" },
		{ CPU_FEATURE_SHA_NI, "sha_ni" },
		{ CPU_FEATURE_AVX512BW, "avx512bw" },
		{ CPU_FEATURE_AVX512VL, "avx512vl" },
		{ CPU_FEATURE_SGX, "sgx" },
		{ CPU_FEATURE_RDSEED, "rdseed" },
		{ CPU_FEATURE_ADX, "adx" },
		{ CPU_FEATURE_AVX512VNNI, "avx512vnni" },
		{ CPU_FEATURE_AVX512VBMI, "avx512vbmi" },
		{ CPU_FEATURE_AVX512VBMI2, "avx512vbmi2" },
		{ CPU_FEATURE_HYPERVISOR, "hypervisor" },
	};
	unsigned i, n = COUNT_OF(matchtable);
	if (n != NUM_CPU_FEATURES) {
		// Warning: incomplete library
		// feature matchtable size differs from the actual number of features.
	}
	for (i = 0; i < n; i++)
		if (matchtable[i].feature == feature)
			return matchtable[i].name;
	return "";
}

const char* cpuid_error(void)
{
	const struct { cpu_error_t error; const char *description; }
	matchtable[] = {
		{ ERR_OK       , "No error"},
		{ ERR_NO_CPUID , "CPUID instruction is not supported"},
		{ ERR_NO_RDTSC , "RDTSC instruction is not supported"},
		{ ERR_NO_MEM   , "Memory allocation failed"},
		{ ERR_OPEN     , "File open operation failed"},
		{ ERR_BADFMT   , "Bad file format"},
		{ ERR_NOT_IMP  , "Not implemented"},
		{ ERR_CPU_UNKN , "Unsupported processor"},
		{ ERR_NO_RDMSR , "RDMSR instruction is not supported"},
		{ ERR_NO_DRIVER, "RDMSR driver error (generic)"},
		{ ERR_NO_PERMS , "No permissions to install RDMSR driver"},
		{ ERR_EXTRACT  , "Cannot extract RDMSR driver (read only media?)"},
		{ ERR_HANDLE   , "Bad handle"},
		{ ERR_INVMSR   , "Invalid MSR"},
		{ ERR_INVCNB   , "Invalid core number"},
		{ ERR_HANDLE_R , "Error on handle read"},
		{ ERR_INVRANGE , "Invalid given range"},
		{ ERR_NOT_FOUND, "Requested type not found"},
	};
	unsigned i;
	for (i = 0; i < COUNT_OF(matchtable); i++)
		if (_libcpuid_errno == matchtable[i].error)
			return matchtable[i].description;
	return "Unknown error";
}


const char* cpuid_lib_version(void)
{
	return VERSION;
}

cpu_vendor_t cpuid_get_vendor(void)
{
	static cpu_vendor_t vendor = VENDOR_UNKNOWN;
	uint32_t raw_vendor[4];
	char vendor_str[VENDOR_STR_MAX];

	if(vendor == VENDOR_UNKNOWN) {
		if (!cpuid_present())
			cpuid_set_error(ERR_NO_CPUID);
		else {
			cpu_exec_cpuid(0, raw_vendor);
			vendor = cpuid_vendor_identify(raw_vendor, vendor_str);
		}
	}
	return vendor;
}

void cpuid_free_raw_data_array(struct cpu_raw_data_array_t* raw_array)
{
	if (raw_array->num_raw <= 0) return;
	free(raw_array->raw);
	raw_array->num_raw = 0;
}

void cpuid_free_system_id(struct system_id_t* system)
{
	if (system->num_cpu_types <= 0) return;
	free(system->cpu_types);
	system->num_cpu_types = 0;
}
