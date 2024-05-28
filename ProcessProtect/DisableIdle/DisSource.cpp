// LoadDriverByDll.cpp : ���ļ����� "main" ����������ִ�н��ڴ˴���ʼ��������
//

#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <psapi.h>

using namespace std;

extern "C" typedef BOOL(__cdecl* LoadDriver)(char* lpszDriverName, char* lpszDriverPath);
extern "C" typedef BOOL(__cdecl* UnloadDriver)(char* szSvrName);
extern "C" typedef BOOL(__cdecl* DeviceControl)(_In_ char* lpszDriverName, _In_ LPWSTR ProtectProcessName, _In_ ULONG_PTR ProtectProcessPid);
extern "C" typedef int(__cdecl* GetProcessState)(DWORD dwProcessID);





HANDLE hToken;
LUID DebugNameValue;
TOKEN_PRIVILEGES Privileges;



//������̣�����δ��������NtSuspendProcess��suspend������������/�ָ� ����ʹ��  
typedef NTSTATUS(WINAPI* NtSuspendProcess)(IN HANDLE Process);
typedef NTSTATUS(WINAPI* NtResumeProcess)(IN HANDLE Process);
BOOL SuspendProcess(DWORD dwProcessID, BOOL suspend) {
	NtSuspendProcess mNtSuspendProcess;
	NtResumeProcess mNtResumeProcess;
	HMODULE ntdll = GetModuleHandle(L"ntdll.dll");
	HANDLE handle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, dwProcessID);
	if (suspend) {
		mNtSuspendProcess = (NtSuspendProcess)GetProcAddress(ntdll, "NtSuspendProcess");
		return mNtSuspendProcess(handle) == 0;
	}
	else {
		mNtResumeProcess = (NtResumeProcess)GetProcAddress(ntdll, "NtResumeProcess");
		return mNtResumeProcess(handle) == 0;
	}
}


/*
��������Ȩ��
*/
bool improvePv()
{
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken)) return false;
	if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) return false;
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, NULL, NULL, NULL)) return false;
	return true;
}
/*
ע��
*/
bool logOffProc()
{
	if (!improvePv() || !ExitWindowsEx(EWX_LOGOFF | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION)) return false;
	return true;
}
/*
����
*/
bool reBootProc()
{
	if (!improvePv() || !ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION)) return false;
	return true;
}

//����cmd����
void HideWindow()
{
	HWND hwnd = GetForegroundWindow();
	if (hwnd)

	{
		ShowWindow(hwnd, SW_HIDE);
	}
}


//ͨ�����ֻ�ȡ����pid�����Ҽ����PEָ���Ƿ�Ϊ��ǰ��ʶ
//����FALSE��������ԭ�򣺽��̸��������С����̷������˳�(���������ܣ�������ѯ�������Ƿ�idle)

INT CheckTargetProcessID_PE(IN PCWSTR ProtectProcessName = L"RTCDesktop.exe")
{
	// ��ȡϵͳ�е����н��̿���
	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;
	HANDLE targetProcessHandle;
	INT result = 0;
	int error_code = 0;

	HMODULE hModule[100] = { 0 };
	BYTE p[1000] = {0};
	//INT P_SIZE = sizeof(p);
	DWORD dwRet = 0;
	DWORD dwNumberOfBytesRead;

	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hProcessSnap == INVALID_HANDLE_VALUE) {
		std::cerr << "Error: CreateToolhelp32Snapshot failed." << std::endl;
		return 0;
	}

	// ���ýṹ���С
	pe32.dwSize = sizeof(PROCESSENTRY32);

	// ���������б�
	if (Process32First(hProcessSnap, &pe32)) 
	{
		do 
		{
			// ��������
 			std::wstring processName(pe32.szExeFile);

			// ����������������Ƿ�ƥ����Ҫ���ҵ�����
			if (processName == ProtectProcessName)
			{
				std::wcout << L"Process found: " << processName << std::endl;
				
				targetProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID);
				if (targetProcessHandle != NULL && targetProcessHandle != INVALID_HANDLE_VALUE)
				{
					EnumProcessModulesEx(targetProcessHandle, hModule, sizeof(hModule), &dwRet, LIST_MODULES_64BIT);
				
					ReadProcessMemory(targetProcessHandle, *(hModule), &p, sizeof(hModule), (SIZE_T*)&dwNumberOfBytesRead);

				if(dwNumberOfBytesRead)
				{
					if (*(p+0x15) == 0xff)
					{
						result =  pe32.th32ProcessID;
						CloseHandle(targetProcessHandle);
						CloseHandle(hProcessSnap);
						return result;
					}
					else if (!p[0])
					{
						printf("hModule ��ȡʧ��:\n");
						continue;

					}
					else
					{
						CloseHandle(targetProcessHandle);
						CloseHandle(hProcessSnap);
						result = 4;//˵����αװ��RTCDesktop
						return result;
					}
				}
				else
				{
					error_code = GetLastError();
					printf("QueryFullProcessImageName ��ȡfull_pathʧ�ܣ�,������:%d\n",error_code);
					CloseHandle(targetProcessHandle);
					
				}
				}
			}
		} while (Process32Next(hProcessSnap, &pe32));
	}

	CloseHandle(hProcessSnap);

	return result;
}



int main(int argc, char* argv[])
{

	//����һ�������壬����GUID�ģ�����ɹ����򷵻���Ч���ֵ ����GUID
	HANDLE mutexHandle = CreateMutexW(NULL, FALSE, L"Global\\{{E96AE1E5-D4CC-48C5-BD86-A2844E8A6A8D}}");
	if (ERROR_ALREADY_EXISTS == GetLastError())
	{
		if (mutexHandle) {
			CloseHandle(mutexHandle);
		}
		printf("�������У���Ҫ�ٿ�����ʵ��.\n");
		return 0;
	}
	std::cout << "starting" << std::endl;




	int return_num = 0;
	
	//SuspendProcess(3308, 1);
	HideWindow();
	
	HMODULE _hDllInst = LoadLibraryW(L"LoadDriverByPs.dll");
	//��ȡ���̵�״̬
	//����-1����ʾ�����쳣
	//����0����ʾ����û�б�����
	//����1����ʾ���̴��ڹ���״̬
	GetProcessState GET_PROCESS_STATE = (GetProcessState)GetProcAddress(_hDllInst, "GetProcessState");

	if (GET_PROCESS_STATE == 0)return 0;

	for (;;)
	{

		/*
		int pid = 8292;
		printf(argv[1]);
		int PID = atoi(argv[1]);
		printf("\nPID:,%d\n", PID);*/

		INT PID = CheckTargetProcessID_PE();

		return_num = GET_PROCESS_STATE(PID);

		printf("return_num:,%d\n", return_num);

		
		if (return_num || PID == 4)
		{
			reBootProc();
		}
		

		Sleep(60000);
		
	}

	FreeLibrary(_hDllInst);


}


