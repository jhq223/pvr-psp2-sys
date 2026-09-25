#include <kernel.h>
#include <stdio.h>

#include "psp2_pvr_defs.h"

#define PSP2_EXTENDED_HEAP_SIZE SCE_KERNEL_1MiB

int _PVRSRVCreateUserModeHeap(SceClibMspace mspace);
SceUID sceKernelGetModuleIdByAddr(const void *module_addr);

int module_stop(SceSize argc, const void *args)
{
	return SCE_KERNEL_STOP_SUCCESS;
}

int module_exit()
{
	return SCE_KERNEL_STOP_SUCCESS;
}

int _sceGpuUserStart(SceClibMspace *mspace, SceUID *memblockId)
{
	SceInt32 ret = SCE_KERNEL_START_SUCCESS;
	ScePVoid base;

	*memblockId = sceKernelAllocMemBlock("SceGpuUserHeap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, PSP2_EXTENDED_HEAP_SIZE, 0);
	if (*memblockId < 0) {
		printf("_sceGpuUserStart: Failed to allocate heap storage with error 0x%X\n", *memblockId);
		ret = SCE_KERNEL_START_NO_RESIDENT;
	}
	else {
		ret = sceKernelGetMemBlockBase(*memblockId, &base);
		if (ret < 0) {
			printf("_sceGpuUserStart: Failed to get address of heap storage with error 0x%X\n", ret);
			sceKernelFreeMemBlock(*memblockId);
			ret = SCE_KERNEL_START_NO_RESIDENT;
		}
		else {
			*mspace = sceClibMspaceCreate(base, PSP2_EXTENDED_HEAP_SIZE);
			if (*mspace == SCE_NULL) {
				printf("_sceGpuUserStart: Failed to create internal heap\n");
				sceKernelFreeMemBlock(*memblockId);
				ret = SCE_KERNEL_START_NO_RESIDENT;
			}
			else {
				ret = SCE_KERNEL_START_SUCCESS;
			}
		}
	}

	return ret;
}

int module_start(SceSize argc, void *args)
{
	SceInt32 ret;
	PVRSRV_CONNECTION* psConnection;
	SceUID gpuEs4Modid = SCE_UID_INVALID_UID;
	SceUID uheapMemblockId = SCE_UID_INVALID_UID;
	SceClibMspace uheapMspace = SCE_NULL;
	ScePVoid dataAddr = SCE_NULL;
	SceKernelModuleInfo modInfo;
	SceClibMspace oldMspace;
	SceUID oldMemblockId;

	if (PVRSRVConnect(&psConnection, 0) != PVRSRV_OK)
		return SCE_KERNEL_START_NO_RESIDENT;

	gpuEs4Modid = sceKernelGetModuleIdByAddr(psConnection);
	PVRSRVDisconnect(psConnection);
	if (gpuEs4Modid < 0)
		return SCE_KERNEL_START_NO_RESIDENT;

	sceClibMemset(&modInfo, 0, sizeof(modInfo));
	modInfo.size = sizeof(SceKernelModuleInfo);
	if (sceKernelGetModuleInfo(gpuEs4Modid, &modInfo) < 0 ||
		modInfo.segments[1].vaddr == SCE_NULL || modInfo.segments[1].memsz < 0x10)
		return SCE_KERNEL_START_NO_RESIDENT;

	dataAddr = modInfo.segments[1].vaddr;

	oldMspace = *(SceClibMspace *)((char *)dataAddr + 0x8);
	oldMemblockId = *(SceUID *)((char *)dataAddr + 0xC);
	if (oldMspace == SCE_NULL || oldMemblockId < 0)
		return SCE_KERNEL_START_NO_RESIDENT;

	/* Allocate first: failure must leave the system driver's old heap usable. */
	ret = _sceGpuUserStart(&uheapMspace, &uheapMemblockId);
	if (ret != SCE_KERNEL_START_SUCCESS)
		return ret;

	*(SceClibMspace *)((char *)dataAddr + 0x8) = uheapMspace;
	*(SceUID *)((char *)dataAddr + 0xC) = uheapMemblockId;
	_PVRSRVCreateUserModeHeap(uheapMspace);
	sceClibMspaceDestroy(oldMspace);
	sceKernelFreeMemBlock(oldMemblockId);

	return SCE_KERNEL_START_SUCCESS;
}
