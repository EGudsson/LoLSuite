#include <windows.h>
#include <ShObjIdl_core.h>
#include <Shlobj_core.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <urlmon.h>
#include <wininet.h>
#include <filesystem>
#include <vector>
#include <functional>
#include <mutex>
#include <dwmapi.h>
#include <uxtheme.h>
#include <fstream>
#include "resource.h"

constexpr size_t kFileCount = 157;
std::wstring files[kFileCount] = {
L"Apr2005_d3dx9_25_x64.cab",
L"Apr2005_d3dx9_25_x86.cab",
L"Apr2006_d3dx9_30_x64.cab",
L"Apr2006_d3dx9_30_x86.cab",
L"Apr2006_MDX1_x86_Archive.cab",
L"Apr2006_MDX1_x86.cab",
L"Apr2006_XACT_x64.cab",
L"Apr2006_XACT_x86.cab",
L"Apr2006_xinput_x64.cab",
L"Apr2006_xinput_x86.cab",
L"APR2007_d3dx10_33_x64.cab",
L"APR2007_d3dx10_33_x86.cab",
L"APR2007_d3dx9_33_x64.cab",
L"APR2007_d3dx9_33_x86.cab",
L"APR2007_XACT_x64.cab",
L"APR2007_XACT_x86.cab",
L"APR2007_xinput_x64.cab",
L"APR2007_xinput_x86.cab",
L"Aug2005_d3dx9_27_x64.cab",
L"Aug2005_d3dx9_27_x86.cab",
L"AUG2006_XACT_x64.cab",
L"AUG2006_XACT_x86.cab",
L"AUG2006_xinput_x64.cab",
L"AUG2006_xinput_x86.cab",
L"AUG2007_d3dx10_35_x64.cab",
L"AUG2007_d3dx10_35_x86.cab",
L"AUG2007_d3dx9_35_x64.cab",
L"AUG2007_d3dx9_35_x86.cab",
L"AUG2007_XACT_x64.cab",
L"AUG2007_XACT_x86.cab",
L"Aug2008_d3dx10_39_x64.cab",
L"Aug2008_d3dx10_39_x86.cab",
L"Aug2008_d3dx9_39_x64.cab",
L"Aug2008_d3dx9_39_x86.cab",
L"Aug2008_XACT_x64.cab",
L"Aug2008_XACT_x86.cab",
L"Aug2008_XAudio_x64.cab",
L"Aug2008_XAudio_x86.cab",
L"Aug2009_D3DCompiler_42_x64.cab",
L"Aug2009_D3DCompiler_42_x86.cab",
L"Aug2009_d3dcsx_42_x64.cab",
L"Aug2009_d3dcsx_42_x86.cab",
L"Aug2009_d3dx10_42_x64.cab",
L"Aug2009_d3dx10_42_x86.cab",
L"Aug2009_d3dx11_42_x64.cab",
L"Aug2009_d3dx11_42_x86.cab",
L"Aug2009_d3dx9_42_x64.cab",
L"Aug2009_d3dx9_42_x86.cab",
L"Aug2009_XACT_x64.cab",
L"Aug2009_XACT_x86.cab",
L"Aug2009_XAudio_x64.cab",
L"Aug2009_XAudio_x86.cab",
L"Dec2005_d3dx9_28_x64.cab",
L"Dec2005_d3dx9_28_x86.cab",
L"DEC2006_d3dx10_00_x64.cab",
L"DEC2006_d3dx10_00_x86.cab",
L"DEC2006_d3dx9_32_x64.cab",
L"DEC2006_d3dx9_32_x86.cab",
L"DEC2006_XACT_x64.cab",
L"DEC2006_XACT_x86.cab",
L"DSETUP.dll",
L"dsetup32.dll",
L"dxdllreg_x86.cab",
L"DXSETUP.exe",
L"dxupdate.cab",
L"Feb2005_d3dx9_24_x64.cab",
L"Feb2005_d3dx9_24_x86.cab",
L"Feb2006_d3dx9_29_x64.cab",
L"Feb2006_d3dx9_29_x86.cab",
L"Feb2006_XACT_x64.cab",
L"Feb2006_XACT_x86.cab",
L"FEB2007_XACT_x64.cab",
L"FEB2007_XACT_x86.cab",
L"Feb2010_X3DAudio_x64.cab",
L"Feb2010_X3DAudio_x86.cab",
L"Feb2010_XACT_x64.cab",
L"Feb2010_XACT_x86.cab",
L"Feb2010_XAudio_x64.cab",
L"Feb2010_XAudio_x86.cab",
L"Jun2005_d3dx9_26_x64.cab",
L"Jun2005_d3dx9_26_x86.cab",
L"JUN2006_XACT_x64.cab",
L"JUN2006_XACT_x86.cab",
L"JUN2007_d3dx10_34_x64.cab",
L"JUN2007_d3dx10_34_x86.cab",
L"JUN2007_d3dx9_34_x64.cab",
L"JUN2007_d3dx9_34_x86.cab",
L"JUN2007_XACT_x64.cab",
L"JUN2007_XACT_x86.cab",
L"JUN2008_d3dx10_38_x64.cab",
L"JUN2008_d3dx10_38_x86.cab",
L"JUN2008_d3dx9_38_x64.cab",
L"JUN2008_d3dx9_38_x86.cab",
L"JUN2008_X3DAudio_x64.cab",
L"JUN2008_X3DAudio_x86.cab",
L"JUN2008_XACT_x64.cab",
L"JUN2008_XACT_x86.cab",
L"JUN2008_XAudio_x64.cab",
L"JUN2008_XAudio_x86.cab",
L"Jun2010_D3DCompiler_43_x64.cab",
L"Jun2010_D3DCompiler_43_x86.cab",
L"Jun2010_d3dcsx_43_x64.cab",
L"Jun2010_d3dcsx_43_x86.cab",
L"Jun2010_d3dx10_43_x64.cab",
L"Jun2010_d3dx10_43_x86.cab",
L"Jun2010_d3dx11_43_x64.cab",
L"Jun2010_d3dx11_43_x86.cab",
L"Jun2010_d3dx9_43_x64.cab",
L"Jun2010_d3dx9_43_x86.cab",
L"Jun2010_XACT_x64.cab",
L"Jun2010_XACT_x86.cab",
L"Jun2010_XAudio_x64.cab",
L"Jun2010_XAudio_x86.cab",
L"Mar2008_d3dx10_37_x64.cab",
L"Mar2008_d3dx10_37_x86.cab",
L"Mar2008_d3dx9_37_x64.cab",
L"Mar2008_d3dx9_37_x86.cab",
L"Mar2008_X3DAudio_x64.cab",
L"Mar2008_X3DAudio_x86.cab",
L"Mar2008_XACT_x64.cab",
L"Mar2008_XACT_x86.cab",
L"Mar2008_XAudio_x64.cab",
L"Mar2008_XAudio_x86.cab",
L"Mar2009_d3dx10_41_x64.cab",
L"Mar2009_d3dx10_41_x86.cab",
L"Mar2009_d3dx9_41_x64.cab",
L"Mar2009_d3dx9_41_x86.cab",
L"Mar2009_X3DAudio_x64.cab",
L"Mar2009_X3DAudio_x86.cab",
L"Mar2009_XACT_x64.cab",
L"Mar2009_XACT_x86.cab",
L"Mar2009_XAudio_x64.cab",
L"Mar2009_XAudio_x86.cab",
L"Nov2007_d3dx10_36_x64.cab",
L"Nov2007_d3dx10_36_x86.cab",
L"Nov2007_d3dx9_36_x64.cab",
L"Nov2007_d3dx9_36_x86.cab",
L"NOV2007_X3DAudio_x64.cab",
L"NOV2007_X3DAudio_x86.cab",
L"NOV2007_XACT_x64.cab",
L"NOV2007_XACT_x86.cab",
L"Nov2008_d3dx10_40_x64.cab",
L"Nov2008_d3dx10_40_x86.cab",
L"Nov2008_d3dx9_40_x64.cab",
L"Nov2008_d3dx9_40_x86.cab",
L"Nov2008_X3DAudio_x64.cab",
L"Nov2008_X3DAudio_x86.cab",
L"Nov2008_XACT_x64.cab",
L"Nov2008_XACT_x86.cab",
L"Nov2008_XAudio_x64.cab",
L"Nov2008_XAudio_x86.cab",
L"Oct2005_xinput_x64.cab",
L"Oct2005_xinput_x86.cab",
L"OCT2006_d3dx9_31_x64.cab",
L"OCT2006_d3dx9_31_x86.cab",
L"OCT2006_XACT_x64.cab",
L"OCT2006_XACT_x86.cab"
};

int cb_index = 0;
std::vector<std::wstring> b(200);
HWND hwndPatch, hwndRestore, combo;

// --- Utility Functions ---
static std::wstring JoinPath(const std::wstring& base, const std::wstring& addition) {
	return (std::filesystem::path(base) / addition).wstring();
}

static void AppendPath(int index, const std::wstring& addition) {
	b[index] = JoinPath(b[index], addition);
}

static void CombinePath(int destIndex, int srcIndex, const std::wstring& addition) {
	b[destIndex] = JoinPath(b[srcIndex], addition);
}

// --- Safe Download and Run ---
static void url(const std::wstring& url, int idx) {
	const std::wstring targetUrl = L"https://lolsuite.org/" + url;
	const std::wstring& filePath = b[idx];
	const std::wstring zonePath = filePath + L":Zone.Identifier";
	DeleteUrlCacheEntry(targetUrl.c_str());
	URLDownloadToFile(nullptr, targetUrl.c_str(), filePath.c_str(), 0, nullptr);
	if (std::filesystem::exists(zonePath)) {
		std::error_code ec;
		std::filesystem::remove(zonePath, ec);
	}
}

// --- Process Management ---
static void ExitThread(const std::wstring& name) {
	auto closeHandle = [](HANDLE h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); };
	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(closeHandle)>
		snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), closeHandle);
	if (snapshot.get() == INVALID_HANDLE_VALUE) return;
	PROCESSENTRY32W entry{ .dwSize = sizeof(entry) };
	for (BOOL found = Process32First(snapshot.get(), &entry); found; found = Process32Next(snapshot.get(), &entry)) {
		if (name == entry.szExeFile) {
			std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(closeHandle)>
				process(OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID), closeHandle);
			if (process) TerminateProcess(process.get(), 0);
			break;
		}
	}
}


static bool Is64BitWindows() {
	BOOL isWow64 = FALSE;
	USHORT processMachine = 0, nativeMachine = 0;

	HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
	if (!hKernel32) return false;

	using FnIsWow64Process2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
	auto fnIsWow64Process2 = reinterpret_cast<FnIsWow64Process2>(
		GetProcAddress(hKernel32, "IsWow64Process2"));

	if (fnIsWow64Process2 &&
		fnIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
		return nativeMachine != IMAGE_FILE_MACHINE_I386;
	}

	using FnIsWow64Process = BOOL(WINAPI*)(HANDLE, PBOOL);
	auto fnIsWow64Process = reinterpret_cast<FnIsWow64Process>(
		GetProcAddress(hKernel32, "IsWow64Process"));

	return fnIsWow64Process &&
		fnIsWow64Process(GetCurrentProcess(), &isWow64) &&
		isWow64;
}

static void ExecuteAndWait(SHELLEXECUTEINFO& sei, bool wait = true) {
	if (ShellExecuteEx(&sei) && wait && sei.hProcess) {
		SetPriorityClass(sei.hProcess, HIGH_PRIORITY_CLASS);
		WaitForSingleObject(sei.hProcess, INFINITE);
		CloseHandle(sei.hProcess);
	}
}

static void PowerShell(const std::vector<std::wstring>& commands) {
	for (const auto& cmd : commands) {
		std::wstring args = L"-Command \"" + cmd + L"\"";
		SHELLEXECUTEINFO sei{
			.cbSize = sizeof(SHELLEXECUTEINFO),
			.fMask = SEE_MASK_NOCLOSEPROCESS,
			.lpVerb = L"runas",
			.lpFile = L"powershell.exe",
			.lpParameters = args.c_str(),
			.nShow = SW_HIDE
		};
		ExecuteAndWait(sei);
	}
}

static void Run(const std::wstring& file, const std::wstring& params, bool wait) {
	SHELLEXECUTEINFO sei{
		.cbSize = sizeof(SHELLEXECUTEINFO),
		.fMask = SEE_MASK_NOCLOSEPROCESS,
		.lpVerb = L"open",
		.lpFile = file.c_str(),
		.lpParameters = params.c_str(),
		.nShow = SW_SHOWNORMAL
	};
	ExecuteAndWait(sei, wait);
}

static void EnsureDirectX9Setup() {
	wchar_t systemDir[MAX_PATH + 1];
	bool isInstalled = false;

	// Check if DirectX 9 is installed by attempting to load d3dx9_43.dll
	HMODULE hDX9 = LoadLibrary(L"d3dx9_43.dll");
	if (hDX9) {
		FreeLibrary(hDX9);
		return; // Already installed
	}

	constexpr int tmpIndex = 158;
	constexpr int baseIndex = 0;

	AppendPath(tmpIndex, std::filesystem::current_path().wstring());
	AppendPath(tmpIndex, L"tmp");

	std::filesystem::remove_all(b[tmpIndex]);
	std::filesystem::create_directory(b[tmpIndex]);

	for (size_t i = 0; i < kFileCount; ++i) {
		b[baseIndex + i].clear();
		CombinePath(baseIndex + i, tmpIndex, files[i]);
		url(L"DXSETUP/" + files[i], baseIndex + i);
	}

	bool allFilesPresent = true;
	for (size_t i = 0; i < kFileCount; ++i) {
		if (!std::filesystem::exists(b[baseIndex + i])) {
			allFilesPresent = false;
			break;
		}
	}

	if (allFilesPresent) {
		ExitThread(L"DXSETUP.exe");
		Run(b[baseIndex + 63], L"/silent", true);
	}

	std::filesystem::remove_all(b[tmpIndex]);
}

// --- Instance Limiting ---
class LimitInstance {
	HANDLE Mutex;
public:
	explicit LimitInstance(const std::wstring& mutexName)
		: Mutex(CreateMutex(nullptr, FALSE, mutexName.c_str())) {
	}
	~LimitInstance() {
		if (Mutex) {
			ReleaseMutex(Mutex);
			CloseHandle(Mutex);
		}
	}
	LimitInstance(const LimitInstance&) = delete;
	LimitInstance& operator=(const LimitInstance&) = delete;
	static bool AnotherInstanceRunning() {
		return GetLastError() == ERROR_ALREADY_EXISTS;
	}
};

std::wstring folder(const std::wstring& path) {
	std::wstring message = L"Select: " + path;
	MessageBoxEx(nullptr, message.c_str(), L"LoLSuite", MB_OK, 0);
	b[0].clear();
	std::wstring selectedPath;
	HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hrInit)) return selectedPath;

	IFileDialog* pfd = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
	if (SUCCEEDED(hr) && pfd) {
		DWORD dwOptions;
		pfd->GetOptions(&dwOptions);
		pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);

		if (SUCCEEDED(pfd->Show(nullptr))) {
			IShellItem* psi = nullptr;
			if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
				PWSTR pszPath = nullptr;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
					selectedPath = pszPath;
					CoTaskMemFree(pszPath);
				}
				psi->Release();
			}
		}
		pfd->Release();
	}

	CoUninitialize();
	b[0] = selectedPath;
}

// --- Service Management ---
static void net(const std::wstring& serviceName, bool start, bool restart = false) {
	auto closeHandle = [](SC_HANDLE h) { if (h) CloseServiceHandle(h); };
	std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, decltype(closeHandle)>
		scm(OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS), closeHandle);
	if (!scm) return;
	std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, decltype(closeHandle)>
		svc(OpenService(scm.get(), serviceName.c_str(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS), closeHandle);
	if (!svc) return;
	SERVICE_STATUS status{};
	if (restart) {
		if (ControlService(svc.get(), SERVICE_CONTROL_STOP, &status)) {
			while (status.dwCurrentState != SERVICE_STOPPED) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if (!QueryServiceStatus(svc.get(), &status)) break;
			}
		}
		StartService(svc.get(), 0, nullptr);
	}
	else if (start) {
		StartService(svc.get(), 0, nullptr);
	}
	else {
		if (ControlService(svc.get(), SERVICE_CONTROL_STOP, &status)) {
			while (status.dwCurrentState != SERVICE_STOPPED) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if (!QueryServiceStatus(svc.get(), &status)) break;
			}
		}
	}
}

// --- Game and Task Management ---
static void manageGame(const std::wstring& game, bool restore) {
	if (game == L"leagueoflegends") {
		folder(L"C:\\Riot Games");
		for (const auto& proc : {
			L"LeagueClient.exe", L"LeagueClientUx.exe", L"LeagueClientUxRender.exe",
			L"League of Legends.exe", L"Riot Client.exe", L"RiotClientServices.exe",
			L"RiotClientCrashHandler.exe", L"LeagueCrashHandler64.exe"
			}) ExitThread(proc);
		CombinePath(56, 0, L"Riot Client\\RiotClientElectron\\Riot Client.exe");
		AppendPath(0, L"League of Legends");
		for (const auto& [i, f] : std::vector<std::pair<int, std::wstring>>{
			{42,L"concrt140.dll"}, {43,L"d3dcompiler_47.dll"}, {44,L"msvcp140.dll"},
			{45,L"msvcp140_1.dll"}, {46,L"msvcp140_2.dll"}, {47,L"msvcp140_codecvt_ids.dll"},
			{48,L"ucrtbase.dll"}, {49,L"vcruntime140.dll"}, {50,L"vcruntime140_1.dll"}
			}) {
			CombinePath(i, 0, f);
			url(restore ? L"restore/lol/" + f : L"patch/" + f, i);
		}
		CombinePath(51, 0, L"Game");
		CombinePath(53, 51, L"D3DCompiler_47.dll");
		CombinePath(55, 51, L"tbb.dll");
		CombinePath(54, 0, L"d3dcompiler_47.dll");
		if (restore)
			std::filesystem::remove(b[55]);
		else
			url(Is64BitWindows() ? L"patch/tbb.dll" : L"patch/tbb_x86.dll", 55);
		auto d3dPath = restore ? L"restore/lol/D3DCompiler_47.dll" :
			(Is64BitWindows() ? L"patch/D3DCompiler_47.dll" : L"patch/D3DCompiler_47_x86.dll");
		url(d3dPath, 53);
		url(d3dPath, 54);
		Run(b[56], L"", false);
	}
	else if (game == L"dota2") {
		folder(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\dota 2 beta");
		ExitThread(L"dota2.exe");
		AppendPath(0, L"game\\bin\\win64");
		CombinePath(1, 0, L"embree3.dll");
		CombinePath(2, 0, L"d3dcompiler_47.dll");
		url(restore ? L"restore/dota2/embree3.dll" : L"patch/embree4.dll", 1);
		url(restore ? L"restore/dota2/d3dcompiler_47.dll" : L"patch/D3DCompiler_47.dll", 2);
		Run(L"steam://rungameid/570//-high -dx11 -fullscreen/", L"", false);
	}
	else if (game == L"smite2") {
		folder(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SMITE 2");
		AppendPath(0, L"Windows");
		for (const auto& proc : { L"Hemingway.exe", L"Hemingway-Win64-Shipping.exe" })
			ExitThread(proc);
		CombinePath(8, 0, L"Engine\\Binaries\\Win64");
		CombinePath(7, 0, L"Hemingway\\Binaries\\Win64");
		CombinePath(1, 8, L"tbb.dll");
		CombinePath(2, 8, L"tbbmalloc.dll");
		CombinePath(3, 7, L"tbb.dll");
		CombinePath(4, 7, L"tbbmalloc.dll");
		url(restore ? L"restore/smite2/tbb.dll" : L"patch/tbb.dll", 1);
		url(restore ? L"restore/smite2/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 2);
		url(restore ? L"restore/smite2/tbb.dll" : L"patch/tbb.dll", 3);
		url(restore ? L"restore/smite2/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 4);
		Run(L"steam://rungameid/2437170", L"", false);
	}
	else if (game == L"mgsΔ") {
		folder(L"METAL GEAR SOLID Δ Install Base Dir");
		for (const auto& proc : { L"MGSDelta.exe", L"MGSDelta-Win64-Shipping.exe", L"Nightmare-Win64-Shipping.exe" })
			ExitThread(proc);
		CombinePath(8, 0, L"MGSDelta\\Binaries\\Win64");
		CombinePath(7, 0, L"MGSDelta_Nightmare\\Binaries\\Win64");
		CombinePath(1, 8, L"tbb.dll");
		CombinePath(2, 8, L"tbb12.dll");
		CombinePath(3, 8, L"tbbmalloc.dll");
		CombinePath(4, 7, L"tbb.dll");
		CombinePath(5, 7, L"tbb12.dll");
		CombinePath(6, 7, L"tbbmalloc.dll");
		url(restore ? L"restore/mgs/tbb.dll" : L"patch/tbb.dll", 1);
		url(restore ? L"restore/mgs/tbb12.dll" : L"patch/tbb.dll", 2);
		url(restore ? L"restore/mgs/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 3);
		url(restore ? L"restore/mgs/tbb.dll" : L"patch/tbb.dll", 4);
		url(restore ? L"restore/mgs/tbb12.dll" : L"patch/tbb.dll", 5);
		url(restore ? L"restore/mgs/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 6);
		Run(L"steam://rungameid/2417610", L"", false);
	}
	else if (game == L"blands4") {
		folder(L"Borderlands 4 Install Base Dir");
		for (const auto& proc : { L"Borderlands4.exe", L"Borderlands4-Win64-Shipping.exe", L"BL4Launcher.exe" })
			ExitThread(proc);
		CombinePath(8, 0, L"OakGame\\Binaries\\Win64");
		CombinePath(1, 8, L"tbb.dll");
		CombinePath(2, 8, L"tbbmalloc.dll");
		url(restore ? L"restore/blands4/tbb.dll" : L"patch/tbb.dll", 1);
		url(restore ? L"restore/blands4//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 2);

		CombinePath(7, 0, L"Engine\\Binaries\\Win64");
		CombinePath(3, 7, L"tbb.dll");
		CombinePath(4, 7, L"tbbmalloc.dll");
		url(restore ? L"restore/blands4/tbb.dll" : L"patch/tbb.dll", 3);
		url(restore ? L"restore/blands4//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 4);

		Run(L"steam://rungameid/1285190", L"", false);
	}
	else if (game == L"oblivionr") {
		folder(L"Oblivion Remastered Install Base Dir");
		for (const auto& proc : { L"OblivionRemastered.exe", L"OblivionRemastered-Win64-Shipping.exe" })
			ExitThread(proc);

		CombinePath(7, 0, L"Engine\\Binaries\\Win64");
		CombinePath(3, 7, L"tbb.dll");
		CombinePath(4, 7, L"tbbmalloc.dll");
		url(restore ? L"restore/oblivionr/tbb.dll" : L"patch/tbb.dll", 3);
		url(restore ? L"restore/oblivionr//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 4);

		CombinePath(8, 0, L"OblivionRemastered\\Binaries\\Win64");
		CombinePath(5, 8, L"tbb.dll");
		CombinePath(6, 8, L"tbbmalloc.dll");
		CombinePath(9, 8, L"tbb12.dll");
		url(restore ? L"restore/oblivionr/tbb.dll" : L"patch/tbb.dll", 5);
		url(restore ? L"restore/oblivionr//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 6);
		url(restore ? L"restore/oblivionr/tbb12.dll" : L"patch/tbb.dll", 9);

		Run(L"steam://rungameid/2623190", L"", false);
	}
	exit(0);
}


static void manageTasks(const std::wstring& task) {
	if (task == L"cafe") {
		net(L"W32Time", false, true);
		for (const auto& proc : {
			L"cmd.exe", L"pwsh.exe", L"powershell.exe", L"WindowsTerminal.exe", L"OpenConsole.exe", L"wt.exe",
			L"Battle.net.exe", L"steam.exe", L"Origin.exe", L"EADesktop.exe", L"EpicGamesLauncher.exe",
			L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe",
			L"Minecraft.Windows.exe"
			}) ExitThread(proc);
		PowerShell({
			L"w32tm /resync",
			L"powercfg -restoredefaultschemes",
			L"powercfg /h off",
			L"wsreset -i",
			L"Add-WindowsCapability -Online -Name NetFx3~~~~",
			L"Update-MpSignature -UpdateSource MicrosoftUpdateServer",
			L"Get-AppxPackage -Name Microsoft.DesktopAppInstaller | Foreach { Add-AppxPackage -DisableDevelopmentMode -Register \"$($_.InstallLocation)\\AppXManifest.xml\" }",
			L"Get-AppxPackage -AllUsers | ForEach-Object { Add-AppxPackage -DisableDevelopmentMode -Register \"$($_.InstallLocation)\\AppxManifest.xml\" }",
			L"winget source update",
			L"winget upgrade --all --accept-package-agreements --accept-source-agreements"
			});
		std::vector<std::wstring> services = { L"wuauserv", L"BITS", L"CryptSvc" };
		for (auto& s : services) net(s, false);
		WCHAR winDir[MAX_PATH + 1];
		if (GetWindowsDirectory(winDir, MAX_PATH + 1)) {
			std::filesystem::remove_all(std::filesystem::path(winDir) / L"SoftwareDistribution");
		}
		for (auto& s : services) net(s, true);
		WCHAR localAppData[MAX_PATH + 1];
		if (SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
			std::filesystem::path explorerPath = std::filesystem::path(localAppData) / L"Microsoft\\Windows\\Explorer";
			for (auto& pattern : { L"thumbcache_*.db", L"iconcache_*.db", L"ExplorerStartupLog*.etl" }) {
				WIN32_FIND_DATA data;
				HANDLE hFind = FindFirstFile((explorerPath / pattern).c_str(), &data);
				if (hFind != INVALID_HANDLE_VALUE) {
					do std::filesystem::remove(explorerPath / data.cFileName);
					while (FindNextFile(hFind, &data));
					FindClose(hFind);
				}
			}
		}
		std::vector<std::wstring> apps = {
			L"Microsoft.VCRedist.2005.x86", L"Microsoft.VCRedist.2008.x64", L"Microsoft.VCRedist.2008.x86",
			L"Microsoft.VCRedist.2010.x64", L"Microsoft.VCRedist.2010.x86", L"Microsoft.VCRedist.2012.x64",
			L"Microsoft.VCRedist.2012.x86", L"Microsoft.VCRedist.2013.x64", L"Microsoft.VCRedist.2013.x86",
			L"Microsoft.VCRedist.2015+.x64", L"Microsoft.PowerShell", L"Microsoft.WindowsTerminal",
			L"9MZPRTH5C0TB", L"9MZ1SNWT0N5D", L"9N4D0MSMP0PT", L"9N5TDP8VCMHS", L"9N95Q1ZZPMH4", L"9NCTDW2W1BH8", L"9NQPSL29BFFF", L"9PB0TRCNRHFX",
			L"9PCSD6N03BKV", L"9PG2DK419DRG", L"9PMMSR1CGPWG", L"Blizzard.BattleNet", L"ElectronicArts.EADesktop",
			L"ElectronicArts.Origin", L"EpicGames.EpicGamesLauncher", L"Valve.Steam", L"Microsoft.VCRedist.2005.x64"
		};
		std::vector<std::wstring> uninstall, install;
		for (auto& app : apps) {
			uninstall.push_back(L"winget uninstall " + app + L" --purge");
			if (app != L"ElectronicArts.Origin") {
				std::wstring cmd = L"winget install " + app + L" --accept-package-agreements --accept-source-agreements";
				if (app == L"Blizzard.BattleNet") cmd += L" --location \"C:\\Battle.Net\"";
				install.push_back(cmd);
			}
		}
		PowerShell(uninstall);
		PowerShell(install);
		char appdata[MAX_PATH + 1];
		size_t size = 0;
		getenv_s(&size, appdata, MAX_PATH + 1, "APPDATA");
		std::filesystem::path configPath = std::filesystem::path(appdata) / ".minecraft";
		std::filesystem::remove_all(configPath);
		configPath /= "launcher_profiles.json";
		std::vector<std::wstring> cmds = {
			L"winget uninstall Mojang.MinecraftLauncher --purge -h",
			L"winget install Oracle.JDK.25 --accept-package-agreements",
			L"winget install Mojang.MinecraftLauncher --accept-package-agreements"
		};
		for (auto* v : { L"JavaRuntimeEnvironment", L"JDK.17", L"JDK.18", L"JDK.19", L"JDK.20", L"JDK.21", L"JDK.22", L"JDK.23", L"JDK.24" })
			cmds.emplace_back(L"winget uninstall Oracle." + std::wstring(v) + L" --purge -h");
		PowerShell(cmds);
		Run(L"C:\\Program Files (x86)\\Minecraft Launcher\\MinecraftLauncher.exe", L"", false);
		while (!std::filesystem::exists(configPath)) std::this_thread::sleep_for(std::chrono::milliseconds(100));
		for (const auto& proc : {
			L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe", L"Minecraft.Windows.exe"
			}) ExitThread(proc);
		std::wifstream in(configPath);
		in.imbue(std::locale("en_US.UTF-8"));
		std::wstring config((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
		in.close();
		std::wstring updated;
		std::wstringstream ss(config);
		std::wstring line;
		while (std::getline(ss, line)) {
			if (line.find(L"\"javaDir\"") == std::wstring::npos && line.find(L"\"skipJreVersionCheck\"") == std::wstring::npos)
				updated += line + L"\n";
		}
		std::wstring jdkpath = L"C:\\\\Program Files\\\\Java\\\\jdk-25\\\\bin\\\\javaw.exe";
		for (auto& type : { L"\"type\" : \"latest-release\"", L"\"type\" : \"latest-snapshot\"" }) {
			size_t pos = updated.find(type);
			if (pos != std::wstring::npos) {
				size_t start = updated.rfind(L'\n', pos);
				if (start != std::wstring::npos) updated.insert(start + 1, L" \"skipJreVersionCheck\" : true,\n");
				size_t javaDirPos = pos;
				for (int i = 0; i < 4 && javaDirPos != std::wstring::npos; ++i)
					javaDirPos = updated.rfind(L'\n', javaDirPos - 1);
				if (javaDirPos != std::wstring::npos)
					updated.insert(javaDirPos + 1, L" \"javaDir\" : \"" + jdkpath + L"\",\n");
			}
		}
		std::wofstream out(configPath);
		out.imbue(std::locale("en_US.UTF-8"));
		out << updated;
		out.close();
	}
	else if (task == L"clear_caches")
	{
		// Flush DNS cache
		if (HMODULE dnsapi = LoadLibrary(L"dnsapi.dll")) {
			using DnsFlushResolverCacheFuncPtr = BOOL(WINAPI*)();
			if (auto DnsFlush = reinterpret_cast<DnsFlushResolverCacheFuncPtr>(
				GetProcAddress(dnsapi, "DnsFlushResolverCache"))) {
				DnsFlush();
			}
			FreeLibrary(dnsapi);
		}

		// Kill browser processes
		for (const auto& proc : { L"firefox.exe", L"msedge.exe", L"chrome.exe", L"iexplore.exe" }) {
			ExitThread(proc);
		}

		// Clear browsing history
		ShellExecuteW(nullptr, L"open", L"RunDll32.exe",
			L"InetCpl.cpl, ClearMyTracksByProcess 4351",
			nullptr, SW_HIDE);

		// Clear browser caches
		auto clearCache = [](const std::filesystem::path& path) {
			if (std::filesystem::exists(path)) std::filesystem::remove_all(path);
			};

		wchar_t localAppData[MAX_PATH + 1];
		if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
			clearCache(std::filesystem::path(localAppData) / "Microsoft" / "Edge" / "User Data" / "Default" / "Cache");
			clearCache(std::filesystem::path(localAppData) / "Google" / "Chrome" / "User Data" / "Default" / "Cache");
		}

		wchar_t roamingAppData[MAX_PATH + 1];
		if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, 0, roamingAppData))) {
			std::filesystem::path profilesDir = std::filesystem::path(roamingAppData) / "Mozilla" / "Firefox" / "Profiles";
			if (std::filesystem::exists(profilesDir)) {
				for (const auto& entry : std::filesystem::directory_iterator(profilesDir)) {
					if (std::filesystem::is_directory(entry)) {
						clearCache(entry.path() / "cache2");
					}
				}
			}
		}

		// Enable cleanup flags in registry
		const wchar_t* regPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VolumeCaches";
		HKEY hKey;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
			DWORD enable = 1;
			wchar_t subKeyName[256];
			DWORD subKeyLen;
			for (DWORD i = 0;; ++i) {
				subKeyLen = 256;
				if (RegEnumKeyEx(hKey, i, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
					break;
				std::wstring fullPath = std::wstring(regPath) + L"\\" + subKeyName;
				HKEY hSubKey;
				if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, fullPath.c_str(), 0, KEY_SET_VALUE, &hSubKey) == ERROR_SUCCESS) {
					RegSetValueEx(hSubKey, L"StateFlags001", 0, REG_DWORD,
						reinterpret_cast<const BYTE*>(&enable), sizeof(enable));
					RegCloseKey(hSubKey);
				}
			}
			RegCloseKey(hKey);
		}

		// Empty recycle bin
		SHEmptyRecycleBin(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);

		// Run disk cleanup on fixed drives
		DWORD driveMask = GetLogicalDrives();
		for (wchar_t drive = L'A'; drive <= L'Z'; ++drive) {
			if (!(driveMask & (1 << (drive - L'A')))) continue;
			std::wstring root = std::wstring(1, drive) + L":\\";
			if (GetDriveType(root.c_str()) != DRIVE_FIXED) continue;

			SHELLEXECUTEINFO sei{
				.cbSize = sizeof(SHELLEXECUTEINFO),
				.fMask = SEE_MASK_NOASYNC,
				.lpVerb = L"open",
				.lpFile = L"cleanmgr.exe",
				.lpParameters = L"/sagerun:1",
				.lpDirectory = root.c_str(),
				.nShow = SW_SHOWNORMAL
			};
			ShellExecuteEx(&sei);
		}
	}
	exit(0);
}

static void handleCommand(int cb, bool flag) {
	static const std::unordered_map<int, std::function<void()>> commandMap = {
		{0, [flag]() { manageGame(L"leagueoflegends", flag); }},
		{1, [flag]() { manageGame(L"dota2", flag); }},
		{2, [flag]() { manageGame(L"smite2", flag); }},
		{3, [flag]() { manageGame(L"mgsΔ", flag); }},
		{4, [flag]() { manageGame(L"blands4", flag); }},
		{5, [flag]() { manageGame(L"oblivionr", flag); }},
		{6, []() { manageTasks(L"cafe"); }},
		{7, []() { manageTasks(L"clear_caches"); } }
	};
	if (auto it = commandMap.find(cb); it != commandMap.end()) {
		it->second();
	}
}

// --- Window Procedure ---
static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	constexpr COLORREF kTextColor = RGB(255, 255, 255);
	constexpr COLORREF kButtonText = RGB(200, 200, 200);
	constexpr COLORREF kBackground = RGB(30, 30, 30);
	constexpr UINT ID_RUN = 1;
	constexpr UINT ID_RUN_SILENT = 2;

	auto DrawTextItem = [](HDC hdc, RECT rc, const wchar_t* text, UINT format, COLORREF color) {
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, color);
		DrawText(hdc, text, -1, &rc, format);
		};

	switch (message) {
	case WM_COMMAND: {
		const UINT id = LOWORD(wParam);
		const UINT code = HIWORD(wParam);

		if (code == CBN_SELCHANGE) {
			cb_index = SendMessage(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
		}

		switch (id) {
		case ID_RUN:
			handleCommand(cb_index, false);
			return 0;
		case ID_RUN_SILENT:
			handleCommand(cb_index, true);
			return 0;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			return 0;
		}
		break;
	}

	case WM_CTLCOLORBTN: {
		HDC hdc = reinterpret_cast<HDC>(wParam);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kButtonText);
		return reinterpret_cast<INT_PTR>(GetStockObject(HOLLOW_BRUSH));
	}

	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORMSGBOX:
	case WM_CTLCOLORSCROLLBAR: {
		HDC hdc = reinterpret_cast<HDC>(wParam);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kTextColor);
		static HBRUSH hBrush = CreateSolidBrush(kBackground);
		return reinterpret_cast<INT_PTR>(hBrush);
	}

	case WM_DRAWITEM: {
		const auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
		const bool isComboOrList = (dis->CtlType == ODT_COMBOBOX || dis->CtlType == ODT_LISTBOX);

		HBRUSH hBrush = CreateSolidBrush(kBackground);
		FillRect(dis->hDC, &dis->rcItem, hBrush);
		DeleteObject(hBrush);

		wchar_t text[256] = {};
		if (isComboOrList) {
			SendMessage(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
			DrawTextItem(dis->hDC, dis->rcItem, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kTextColor);
		}
		else {
			GetWindowText(dis->hwndItem, text, _countof(text));
			DrawTextItem(dis->hDC, dis->rcItem, text, DT_CENTER | DT_VCENTER, kButtonText);
		}
		return TRUE;
	}

	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}
int wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	MSG msg;

	// Clear clipboard
	if (OpenClipboard(nullptr)) {
		EmptyClipboard();
		CloseClipboard();
	}

	// Enforce single instance
	LimitInstance GUID(L"{3025d31f-c76e-435c-a4b48-9d084fa9f5ea}");
	if (LimitInstance::AnotherInstanceRunning()) return 0;

	// Register window class
	WNDCLASSEXW wcex{
		sizeof(wcex), CS_HREDRAW | CS_VREDRAW, WndProc,
		0, 0, hInstance,
		LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)),
		LoadCursor(nullptr, IDC_ARROW),
		CreateSolidBrush(RGB(32, 32, 32)),
		nullptr, L"LoLSuite", nullptr
	};
	RegisterClassEx(&wcex);

	// Create main window
	HWND hWnd = CreateWindowEx(
		0, L"LoLSuite", L"LoLSuite GUI",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 420, 100,
		nullptr, nullptr, hInstance, nullptr
	);


	// Create controls
	hwndPatch = CreateWindowEx(
		WS_EX_TOOLWINDOW, L"BUTTON", L"Patch",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		10, 20, 60, 30, hWnd, HMENU(1), hInstance, nullptr
	);

	hwndRestore = CreateWindowEx(
		0, L"BUTTON", L"Restore",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		75, 20, 60, 30, hWnd, HMENU(2), hInstance, nullptr
	);

	combo = CreateWindowEx(
		0, L"COMBOBOX", nullptr,
		CBS_DROPDOWN | WS_CHILD | WS_VISIBLE,
		160, 20, 240, 300, hWnd, nullptr, hInstance, nullptr
	);

	HFONT hFont = CreateFont(
		-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
		L"Segoe UI"
	);

	SendMessage(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
	SendMessage(hwndPatch, WM_SETFONT, (WPARAM)hFont, TRUE);
	SendMessage(hwndRestore, WM_SETFONT, (WPARAM)hFont, TRUE);


	for (const auto& item : {
		L"League of Legends", L"DOTA 2", L"SMITE 2",
		L"Metal Gear Solid Δ", L"Borderlands 4", L"Oblivion : Remastered", L"Game Clients", L"Clear Caches"
		}) {
		SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
	}
	SendMessage(combo, CB_SETCURSEL, 0, 0);

	// UnblockedDirectX9
	EnsureDirectX9Setup();

	// Show window and run message loop
	ShowWindow(hWnd, nShowCmd);
	UpdateWindow(hWnd);
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return static_cast<int>(msg.wParam);
}