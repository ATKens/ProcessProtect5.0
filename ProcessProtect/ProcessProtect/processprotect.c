#include <ntifs.h>
#include "ProcessInformation.h"
#include "peb.h"


//�궨����

#define DEVICE_NAME L"\\device\\ProcessProtect"
#define LINK_NAME L"\\dosdevices\\ProcessProtect"



#define IOCTRL_BASE 0x800

#define MYIOCTRL_CODE(i) \
	CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTRL_BASE+i, METHOD_BUFFERED,FILE_ANY_ACCESS)

#define CTL_START MYIOCTRL_CODE(0)
#define CTL_REBOOT MYIOCTRL_CODE(1)
#define CTL_CHECK_PE MYIOCTRL_CODE(2)
#define CTL_BYE MYIOCTRL_CODE(3)
#define CTL_HEARTBEAT MYIOCTRL_CODE(4)


#define XOR_SWAP_STRING_ELEMENTS(arr1, arr2, size) \
    do { \
        for (size_t i = 0; i < size; ++i) { \
            (arr1)[i] = (arr1)[i] ^ (arr2)[i]; \
            (arr2)[i] = (arr1)[i] ^ (arr2)[i]; \
            (arr1)[i] = (arr1)[i] ^ (arr2)[i]; \
        } \
    } while (0)

//���������������
#define ACCESS_ZERO (*(volatile int *)0 = 0)




//�ṹ�嶨����
enum FIRMWARE_REENTRY
{
	HalHaltRoutine,
	HalPowerDownRoutine,
	HalRestartRoutine,
	HalRebootRoutine,
	HalInteractiveModeRoutine,
	HalMaximumRoutine
} FIRMWARE_REENTRY, * PFIRMWARE_REENTRY;



typedef struct _DEVICE_EXTENSION {
	volatile LONG missedHeartbeats;
	volatile LONG heartbeatCounter;
	PKTIMER heartbeatTimer;
	KDPC heartbeatDpc;
} DEVICE_EXTENSION, * PDEVICE_EXTENSION;




PVOID pRegistrationHandle;
//���̹�������ϸ�����������
#define PROCESS_TERMINATE_0       0x1001
//taskkillָ���������
#define PROCESS_TERMINATE_1       0x0001 
//taskkillָ���/f����ǿɱ���̽�����
#define PROCESS_KILL_F			  0x1401
//���̹�������������
#define PROCESS_TERMINATE_2       0x1041
// _LDR_DATA_TABLE_ENTRY ,ע��32λ��64λ�Ķ����С
#ifdef _WIN64
typedef struct _LDR_DATA
{
	LIST_ENTRY listEntry;
	ULONG64 __Undefined1;
	ULONG64 __Undefined2;
	ULONG64 __Undefined3;
	ULONG64 NonPagedDebugInfo;
	ULONG64 DllBase;
	ULONG64 EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING path;
	UNICODE_STRING name;
	ULONG   Flags;
}LDR_DATA, * PLDR_DATA;
#else
typedef struct _LDR_DATA
{
	LIST_ENTRY listEntry;
	ULONG unknown1;
	ULONG unknown2;
	ULONG unknown3;
	ULONG unknown4;
	ULONG unknown5;
	ULONG unknown6;
	ULONG unknown7;
	UNICODE_STRING path;
	UNICODE_STRING name;
	ULONG   Flags;
}LDR_DATA, * PLDR_DATA;
#endif


typedef   enum   _SHUTDOWN_ACTION {
	ShutdownNoReboot,         //�ػ�������
	ShutdownReboot,             //�ػ�������
	ShutdownPowerOff          //�ػ����رյ�Դ
}SHUTDOWN_ACTION;




//����������


#if DBG
EXTERN_C void AsmInt3();
#endif



typedef void (*VoidFunctionPointer)();

VOID HalReturnToFirmware(
	IN enum FIRMWARE_REENTRY  Routine
);

VOID CreateSystemThreadCommonInterface(VoidFunctionPointer function, PDEVICE_OBJECT pObject);

VOID ShutDownRuntimeDirect();

VOID MyTimerProcess(__in struct _KDPC* Dpc, __in_opt PVOID DeferredContext, __in_opt PVOID SystemArgument1, __in_opt PVOID SystemArgument2);


LONG count = 0;
KTIMER g_ktimer;
KDPC g_kdpc;


NTSTATUS DispatchCommon(PDEVICE_OBJECT pObject, PIRP pIrp)
{
	pIrp->IoStatus.Status = STATUS_SUCCESS; // ���ظ�Ӧ�ò�
	pIrp->IoStatus.Information = 0; // ��д�ֽ���

	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	return STATUS_SUCCESS; // ���ظ��ں˲�IO������
}

NTSTATUS CreateDispatch(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp
)
{
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}




NTSTATUS DispatchIoctrl(PDEVICE_OBJECT pObject, PIRP pIrp)
{
	ULONG uIoctrlCode = 0;
	PVOID pInputBuff = NULL;
	PVOID pOutputBuff = NULL;

	ULONG uInputLength = 0;
	ULONG uOutputLength = 0;
	PIO_STACK_LOCATION pStack = NULL;

	pInputBuff = pOutputBuff = pIrp->AssociatedIrp.SystemBuffer;

	pStack = IoGetCurrentIrpStackLocation(pIrp);
	uInputLength = pStack->Parameters.DeviceIoControl.InputBufferLength;
	uOutputLength = pStack->Parameters.DeviceIoControl.OutputBufferLength;

	
	uIoctrlCode = pStack->Parameters.DeviceIoControl.IoControlCode;

	PDEVICE_EXTENSION deviceExtension = pObject->DeviceExtension;

	switch (uIoctrlCode)
	{

	case CTL_REBOOT://ֱ�������ӿ�

#if DBG
		AsmInt3();
#endif
		CreateSystemThreadCommonInterface(ShutDownRuntimeDirect, pObject);
		break;

	case CTL_HEARTBEAT:
		InterlockedIncrement(&deviceExtension->heartbeatCounter);

		DbgPrint("CTL_HEARTBEAT\n");

		break;
	default:
		DbgPrint("Unknown iocontrol\n");

	}

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;

}


VOID DriverUnload(PDRIVER_OBJECT pDriverObject)
{
	
	DbgPrint("Driver unloaded\n");


	HalReturnToFirmware(HalRebootRoutine);
}


NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject,
	PUNICODE_STRING pRegPath)
{

#if DBG
	AsmInt3();
#endif

	UNICODE_STRING uDeviceName = { 0 };
	UNICODE_STRING uLinkName = { 0 };
	NTSTATUS ntStatus = 0;
	PDEVICE_OBJECT pDeviceObject = NULL;
	ULONG i = 0;

	DbgPrint("Driver load begin\n");

	RtlInitUnicodeString(&uDeviceName, DEVICE_NAME);
	RtlInitUnicodeString(&uLinkName, LINK_NAME);

	ntStatus = IoCreateDevice(pDriverObject,
		sizeof(DEVICE_EXTENSION), &uDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDeviceObject);

	if (!NT_SUCCESS(ntStatus))
	{
		DbgPrint("IoCreateDevice failed:%x", ntStatus);
		return ntStatus;
	}

	//DO_BUFFERED_IO�涨R3��R0֮��read��writeͨ�ŵķ�ʽ��
	//1,buffered io
	//2,direct io
	//3,neither io
	pDeviceObject->Flags |= DO_BUFFERED_IO;

	ntStatus = IoCreateSymbolicLink(&uLinkName, &uDeviceName);
	if (!NT_SUCCESS(ntStatus))
	{
		IoDeleteDevice(pDeviceObject);
		DbgPrint("IoCreateSymbolicLink failed:%x\n", ntStatus);
		return ntStatus;
	}

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION + 1; i++)
	{
		pDriverObject->MajorFunction[i] = DispatchCommon;
	}
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctrl;
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = CreateDispatch;
	pDriverObject->DriverUnload = DriverUnload;
	

	
	


	LARGE_INTEGER la_dutime = {

	0 };

	// ÿ��1��ִ��һ��
	la_dutime.QuadPart = 1000 * 1000 * -10;

	// 1.��ʼ����ʱ������
	KeInitializeTimer(&g_ktimer);

	// 2.��ʼ��DPC��ʱ��
	KeInitializeDpc(&g_kdpc, MyTimerProcess, pDeviceObject->DeviceExtension);

	// 3.�����Զ�ѭ����ʱ��5��һ������,��ʼ��ʱ
	KeSetTimerEx(&g_ktimer, la_dutime, 5000, &g_kdpc);
	
	DbgPrint("Driver load ok!\n");

	return STATUS_SUCCESS;
}



VOID ShutDownRuntimeDirect()
{
	
	KdPrint(("Call NtShutdownSystem.\n"));

	//NtShutdownSystem(ShutdownReboot);
	//ACCESS_ZERO;
	//HalReturnToFirmware(HalPowerDownRoutine);
	HalReturnToFirmware(HalRebootRoutine);
}







VOID CreateSystemThreadCommonInterface(VoidFunctionPointer function, PDEVICE_OBJECT pObject)
{
	HANDLE SD_hThread;
	//PVOID objtowait = 0;
	KdPrint(("In CreateSystemThreadCommonInterface.\n"));
	NTSTATUS dwStatus =
		PsCreateSystemThread(
			&SD_hThread,
			0,
			NULL,
			(HANDLE)0,
			NULL,
			function,
			pObject
		);

	NTSTATUS st;
	if ((KeGetCurrentIrql()) != PASSIVE_LEVEL)
	{
		st = KfRaiseIrql(PASSIVE_LEVEL);

	}
	if ((KeGetCurrentIrql()) != PASSIVE_LEVEL)
	{

		return;
	}
	KdPrint(("Out CreateSystemThreadCommonInterface.\n"));
}




// �Զ��嶨ʱ������
VOID MyTimerProcess(__in struct _KDPC* Dpc, __in_opt PVOID DeferredContext, __in_opt PVOID SystemArgument1, __in_opt PVOID SystemArgument2)
{
#if DBG
	AsmInt3();
#endif
	PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeferredContext;
	LONG count = InterlockedExchange(&deviceExtension->heartbeatCounter, 0); // ���ü���������ȡ��֮ǰ��ֵ

	if (count == 0) {
		InterlockedIncrement(&deviceExtension->missedHeartbeats); // ����δ�յ������Ĵ���
		if (InterlockedCompareExchange(&deviceExtension->missedHeartbeats, 60, 60) >= 60) {
			DbgPrint("No heartbeat received for several intervals.\n");
			HalReturnToFirmware(HalRebootRoutine);

			InterlockedExchange(&deviceExtension->missedHeartbeats, 0); // ����δ�յ������Ĵ���,ʵ�����޷�ִ��
		}
	}
	else {
		DbgPrint("Heartbeat(s) received: %d.\n", count);
		InterlockedExchange(&deviceExtension->missedHeartbeats, 0); // ������������δ�յ������Ĵ���
	}

	
}
