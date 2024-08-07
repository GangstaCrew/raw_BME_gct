/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "kekeo.h"

const KUHL_M * mimikatz_modules[] = {
	&kuhl_m_standard,
	&kuhl_m_tgt,
	&kuhl_m_tgs,
	&kuhl_m_exploit,
	&kuhl_m_misc,
	&kuhl_m_kerberos,
	&kuhl_m_smb,
	&kuhl_m_ntlm,
	&kuhl_m_tsssp,
	&kuhl_m_server,
};

int wmain(int argc, wchar_t * argv[])
{
	NTSTATUS status = STATUS_SUCCESS;
	int i;
#if !defined(_POWERKATZ)
	wchar_t input[0x4000];
#endif
	mimikatz_begin();
	for(i = MIMIKATZ_AUTO_COMMAND_START ; (i < argc) && (status != STATUS_FATAL_APP_EXIT) ; i++)
	{
		kprintf(L"\n" MIMIKATZ L"(" MIMIKATZ_AUTO_COMMAND_STRING L") # %s\n", argv[i]);
		status = mimikatz_dispatchCommand(argv[i]);
	}
#if !defined(_POWERKATZ)
	while ((status != STATUS_PROCESS_IS_TERMINATING) && (status != STATUS_THREAD_IS_TERMINATING))
	{
		kprintf(L"\n" MIMIKATZ L" # "); fflush(stdin);
		if (kread(input, ARRAYSIZE(input)))
		{
			kprintf_inputline(L"%s\n", input);
			status = mimikatz_dispatchCommand(input);
		}
	}
#endif
	mimikatz_end(status);
	return STATUS_SUCCESS;
}

void mimikatz_begin()
{
	kull_m_output_init();
#if !defined(_POWERKATZ)
	SetConsoleTitle(MIMIKATZ L" " MIMIKATZ_VERSION L" " MIMIKATZ_ARCH L" (oe.eo)");
	SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#endif
	kprintf(L"\n"
		L"  ___ _    " MIMIKATZ_FULL L"\n"
		L" /   ('>-  " MIMIKATZ_SECOND L"\n"
		L" | K  |    /* * *\n"
		L" \\____/     Benjamin DELPY `gentilkiwi` ( benjamin@gentilkiwi.com )\n"
		L"  L\\_       https://blog.gentilkiwi.com/kekeo                (oe.eo)\n"
		L"            " MIMIKATZ_SPECIAL L" with %2u modules * * */\n", ARRAYSIZE(mimikatz_modules));
	mimikatz_initOrClean(TRUE);
}

void mimikatz_end(NTSTATUS status)
{
	mimikatz_initOrClean(FALSE);
#if !defined(_POWERKATZ)
	SetConsoleCtrlHandler(HandlerRoutine, FALSE);
#endif
	kull_m_output_clean();
#if !defined(_WINDLL)
	if(status == STATUS_THREAD_IS_TERMINATING)
		ExitThread(STATUS_SUCCESS);
	else ExitProcess(STATUS_SUCCESS);
#endif
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
	mimikatz_initOrClean(FALSE);
	return FALSE;
}

NTSTATUS mimikatz_initOrClean(BOOL Init)
{
	unsigned short indexModule;
	PKUHL_M_C_FUNC_INIT function;
	long offsetToFunc;
	NTSTATUS fStatus;
	HRESULT hr;
	g_isBreak = !Init;
	if(Init)
	{
		RtlGetNtVersionNumbers(&MIMIKATZ_NT_MAJOR_VERSION, &MIMIKATZ_NT_MINOR_VERSION, &MIMIKATZ_NT_BUILD_NUMBER);
		MIMIKATZ_NT_BUILD_NUMBER &= 0x00007fff;
		offsetToFunc = FIELD_OFFSET(KUHL_M, pInit);
		hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if(FAILED(hr))
#ifdef _WINDLL
			if(hr != RPC_E_CHANGED_MODE)
#endif
			PRINT_ERROR(L"CoInitializeEx: %08x\n", hr);
		kull_m_sock_startup();
		kull_m_kerberos_asn1_init();
	}
	else
		offsetToFunc = FIELD_OFFSET(KUHL_M, pClean);

	for(indexModule = 0; indexModule < ARRAYSIZE(mimikatz_modules); indexModule++)
	{
		if(function = *(PKUHL_M_C_FUNC_INIT *) ((ULONG_PTR) (mimikatz_modules[indexModule]) + offsetToFunc))
		{
			fStatus = function();
			if(!NT_SUCCESS(fStatus))
				kprintf(L">>> %s of \'%s\' module failed : %08x\n", (Init ? L"INIT" : L"CLEAN"), mimikatz_modules[indexModule]->shortName, fStatus);
		}
	}

	if(!Init)
	{
		kull_m_kerberos_asn1_term();
		CoUninitialize();
		kull_m_sock_finish();
		kull_m_output_file(NULL);
	}
	return STATUS_SUCCESS;
}

NTSTATUS mimikatz_dispatchCommand(wchar_t * input)
{
	NTSTATUS status;
	PWCHAR full;
	if(full = kull_m_file_fullPath(input))
	{
		switch(full[0])
		{
		default:
			status = mimikatz_doLocal(full);
		}
		LocalFree(full);
	}
	return status;
}

NTSTATUS mimikatz_doLocal(wchar_t * input)
{
	NTSTATUS status = STATUS_SUCCESS;
	int argc;
	wchar_t ** argv = CommandLineToArgvW(input, &argc), *module = NULL, *command = NULL, *match;
	unsigned short indexModule, indexCommand;
	BOOL moduleFound = FALSE, commandFound = FALSE;
	
	if(argv && (argc > 0))
	{
		if(match = wcsstr(argv[0], L"::"))
		{
			if(module = (wchar_t *) LocalAlloc(LPTR, (match - argv[0] + 1) * sizeof(wchar_t)))
			{
				if((unsigned int) (match + 2 - argv[0]) < wcslen(argv[0]))
					command = match + 2;
				RtlCopyMemory(module, argv[0], (match - argv[0]) * sizeof(wchar_t));
			}
		}
		else command = argv[0];

		for(indexModule = 0; !moduleFound && (indexModule < ARRAYSIZE(mimikatz_modules)); indexModule++)
			if(moduleFound = (!module || (_wcsicmp(module, mimikatz_modules[indexModule]->shortName) == 0)))
				if(command)
					for(indexCommand = 0; !commandFound && (indexCommand < mimikatz_modules[indexModule]->nbCommands); indexCommand++)
						if(commandFound = _wcsicmp(command, mimikatz_modules[indexModule]->commands[indexCommand].command) == 0)
							status = mimikatz_modules[indexModule]->commands[indexCommand].pCommand(argc - 1, argv + 1);

		if(!moduleFound)
		{
			PRINT_ERROR(L"\"%s\" module not found !\n", module);
			for(indexModule = 0; indexModule < ARRAYSIZE(mimikatz_modules); indexModule++)
			{
				kprintf(L"\n%16s", mimikatz_modules[indexModule]->shortName);
				if(mimikatz_modules[indexModule]->fullName)
					kprintf(L"  -  %s", mimikatz_modules[indexModule]->fullName);
				if(mimikatz_modules[indexModule]->description)
					kprintf(L"  [%s]", mimikatz_modules[indexModule]->description);
			}
			kprintf(L"\n");
		}
		else if(!commandFound)
		{
			indexModule -= 1;
			PRINT_ERROR(L"\"%s\" command of \"%s\" module not found !\n", command, mimikatz_modules[indexModule]->shortName);

			kprintf(L"\nModule :\t%s", mimikatz_modules[indexModule]->shortName);
			if(mimikatz_modules[indexModule]->fullName)
				kprintf(L"\nFull name :\t%s", mimikatz_modules[indexModule]->fullName);
			if(mimikatz_modules[indexModule]->description)
				kprintf(L"\nDescription :\t%s", mimikatz_modules[indexModule]->description);
			kprintf(L"\n");

			for(indexCommand = 0; indexCommand < mimikatz_modules[indexModule]->nbCommands; indexCommand++)
			{
				kprintf(L"\n%16s", mimikatz_modules[indexModule]->commands[indexCommand].command);
				if(mimikatz_modules[indexModule]->commands[indexCommand].description)
					kprintf(L"  -  %s", mimikatz_modules[indexModule]->commands[indexCommand].description);
			}
			kprintf(L"\n");
		}

		if(module)
			LocalFree(module);
		LocalFree(argv);
	}
	return status;
}

#if defined(_POWERKATZ)
__declspec(dllexport) wchar_t * powershell_reflective_kekeo(LPCWSTR input)
{
	int argc = 0;
	wchar_t ** argv;
	
	if(argv = CommandLineToArgvW(input, &argc))
	{
		outputBufferElements = 0xff;
		outputBufferElementsPosition = 0;
		if(outputBuffer = (wchar_t *) LocalAlloc(LPTR, outputBufferElements * sizeof(wchar_t)))
			wmain(argc, argv);
		LocalFree(argv);
	}
	return outputBuffer;
}
#endif

#if defined(_WINDLL)
void CALLBACK kekeo_dll(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
	int argc = 0;
	wchar_t ** argv;

	AllocConsole();
#pragma warning(push)
#pragma warning(disable:4996)
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	freopen("CONIN$", "r", stdin);
#pragma warning(pop)
	if(lpszCmdLine && lstrlenW(lpszCmdLine))
	{
		if(argv = CommandLineToArgvW(lpszCmdLine, &argc))
		{
			wmain(argc, argv);
			LocalFree(argv);
		}
	}
	else wmain(0, NULL);
}
#endif