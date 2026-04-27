#include <windows.h>
#include <filesystem>
#include <urlmon.h>
#include <ShObjIdl_core.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <wininet.h>
#include <vector>
#include <fstream>
#include <thread>
#include <VersionHelpers.h>
#include "resource.h"
#include <shlobj.h>
#include <dwmapi.h>

inline void DwmSet(HWND hWnd, DWMWINDOWATTRIBUTE attr, auto&& value)
{
	DwmSetWindowAttribute(hWnd, attr, &value, sizeof(value));
}

inline void EnableBackdrop(HWND hWnd, bool Mica)
{
	DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
	DwmSet(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, corner);
	DWM_SYSTEMBACKDROP_TYPE backdrop = Mica ? DWMSBT_MAINWINDOW : DWMSBT_TRANSIENTWINDOW;
	DwmSet(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, backdrop);
}

const wchar_t* font = L"Segoe UI Variable";

int cb_index = 0;
std::vector<std::wstring> b(159);
HWND hWnd, patch, restore, listbox;

static std::wstring JoinP(const std::wstring& base, const std::wstring& addition) {
	return (std::filesystem::path(base) / addition).wstring();
}

static void AppendP(int index, const std::wstring& addition) {
	b[index] = JoinP(b[index], addition);
}

static void CombineP(int destIndex, int srcIndex, const std::wstring& addition) {
	b[destIndex] = JoinP(b[srcIndex], addition);
}

static void Server(const std::wstring& url, int idx) {
	const std::wstring targetUrl = L"https://pub-769810f4ffd448b68be4a51316b03c57.r2.dev/" + url;
	const std::wstring& filePath = b[idx];
	const std::wstring zonePath = filePath + L":Zone.Identifier";
	DeleteUrlCacheEntry(targetUrl.c_str());
	URLDownloadToFile(nullptr, targetUrl.c_str(), filePath.c_str(), 0, nullptr);
	if (std::filesystem::exists(zonePath)) {
		std::error_code ec;
		std::filesystem::remove(zonePath, ec);
	}
}

bool Download(const wchar_t* url, const wchar_t* outPath) {
	HRESULT hr = URLDownloadToFile(nullptr, url, outPath, 0, nullptr);
	return SUCCEEDED(hr);
}

bool Shortcut()
{
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath);
    if (FAILED(hr)) return false;
    wchar_t shortcutPath[MAX_PATH+1];
    swprintf_s(shortcutPath, L"%s\\LoLSuite - FPS Booster.lnk", desktopPath);
    CoTaskMemFree(desktopPath);
    wchar_t exePath[MAX_PATH+1];
    GetModuleFileName(nullptr, exePath, MAX_PATH);
    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr)) return false;
    link->SetPath(exePath);
    link->SetArguments(L"");
    link->SetDescription(L"FPS Booster");
    link->SetIconLocation(exePath, 0);
    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (FAILED(hr)) {
        link->Release();
        return false;
    }
    hr = file->Save(shortcutPath, TRUE);
    file->Release();
    link->Release();
    return SUCCEEDED(hr);
}

bool qInstall(const wchar_t* installerPath)
{
	SHELLEXECUTEINFOW sei = { sizeof(sei) };
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpFile = installerPath;
	sei.lpParameters = L"/Q";
	sei.nShow = SW_HIDE;
	if (!ShellExecuteEx(&sei))
		return false;
	if (sei.hProcess)
	{
		WaitForSingleObject(sei.hProcess, INFINITE);
		CloseHandle(sei.hProcess);
	}

	return true;
}

static void PKill(const std::wstring& name) {
	auto hclose = [](HANDLE h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); };

	std::unique_ptr<void, decltype(hclose)>
		snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), hclose);
	if (snap.get() == INVALID_HANDLE_VALUE) return;

	PROCESSENTRY32W e{ sizeof(e) };
	for (BOOL ok = Process32First(snap.get(), &e); ok; ok = Process32Next(snap.get(), &e)) {
		if (name == e.szExeFile) {
			std::unique_ptr<void, decltype(hclose)>
				proc(OpenProcess(PROCESS_TERMINATE, FALSE, e.th32ProcessID), hclose);
			if (proc.get()) TerminateProcess(proc.get(), 0);
		}
	}
}

static bool x64()
{
	USHORT processMachine = 0, nativeMachine = 0;
	auto k32 = GetModuleHandle(L"kernel32.dll");
	if (!k32) return false;
	using Fn2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
	auto fn2 = reinterpret_cast<Fn2>(GetProcAddress(k32, "IsWow64Process2"));

	if (fn2) {
		if (fn2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
			return nativeMachine == IMAGE_FILE_MACHINE_AMD64 ||
				nativeMachine == IMAGE_FILE_MACHINE_ARM64;
		}
	}
	using Fn = BOOL(WINAPI*)(HANDLE, PBOOL);
	auto fn = reinterpret_cast<Fn>(GetProcAddress(k32, "IsWow64Process"));
	if (fn) {
		BOOL wow = FALSE;
		if (fn(GetCurrentProcess(), &wow))
			return wow;
	}

	return false;
}

static void ExecuteAndWait(SHELLEXECUTEINFO& sei, bool wait = true)
{
	if (!ShellExecuteEx(&sei))
	{
		DWORD err = GetLastError();
		return;
	}

	if (wait && sei.hProcess)
	{
		SetPriorityClass(sei.hProcess, HIGH_PRIORITY_CLASS);
		while (WaitForSingleObject(sei.hProcess, 50) == WAIT_TIMEOUT)
		{
			MSG msg;
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		CloseHandle(sei.hProcess);
		sei.hProcess = nullptr;
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

static void PShell(const std::vector<std::wstring>& cmds)
{
	std::wstring script;
	for (auto& c : cmds) script += c + L"; ";
	std::wstring args = L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"& { " + script + L" }\"";
	wchar_t pwsh[MAX_PATH + 1], winget[MAX_PATH + 1];
	bool hasPwsh = SearchPath(nullptr, L"pwsh.exe", nullptr, MAX_PATH + 1, pwsh, nullptr);
	bool hasWinget = SearchPath(nullptr, L"winget.exe", nullptr, MAX_PATH + 1, winget, nullptr);

	auto run = [&](const wchar_t* file, const wchar_t* p) {
		SHELLEXECUTEINFO s{ sizeof(s) };
		s.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
		s.lpVerb = L"runas";
		s.lpFile = file;
		s.lpParameters = p;
		s.nShow = SW_HIDE;
		if (ShellExecuteEx(&s) && s.hProcess) {
			WaitForSingleObject(s.hProcess, INFINITE);
			CloseHandle(s.hProcess);
		}
		};
	if (!hasWinget) {
		run(L"powershell.exe", L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Get-AppxPackage -Name Microsoft.DesktopAppInstaller | Foreach { Add-AppxPackage -DisableDevelopmentMode -Register '$($_.InstallLocation)\\AppXManifest.xml' }\"");
		hasWinget = SearchPath(nullptr, L"winget.exe", nullptr, MAX_PATH + 1, winget, nullptr);
	}
	if (!hasPwsh) {
			run(L"winget", L"install Microsoft.PowerShell --silent --accept-package-agreements --accept-source-agreements");
			hasPwsh = SearchPath(nullptr, L"pwsh.exe", nullptr, MAX_PATH + 1, pwsh, nullptr);
	}
	const wchar_t* shell = hasPwsh ? L"pwsh.exe" : L"powershell.exe";
	run(shell, args.c_str());
}

std::wstring browse(const std::wstring& pathLabel) {
	std::wstring iniPath = (std::filesystem::current_path() / L"LoLSuite.cfg").wstring();
	wchar_t savedPath[MAX_PATH+1] = {};
	GetPrivateProfileString(pathLabel.c_str(), L"path", L"", savedPath, MAX_PATH, iniPath.c_str());
	if (wcslen(savedPath) > 0) {
		b[0] = savedPath;
		return b[0];
	}
	std::wstring message = L"Select: " + pathLabel;
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
	if (!b[0].empty()) {
		WritePrivateProfileString(pathLabel.c_str(), L"path", b[0].c_str(), iniPath.c_str());
	}

	return b[0];
}

static void service(const std::wstring& serviceName, bool start, bool restart = false) {
	struct ServiceHandleDeleter {
		void operator()(SC_HANDLE h) const {
			if (h) CloseServiceHandle(h);
		}
	};
	using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ServiceHandleDeleter>;
	ServiceHandle scm(OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS));
	ServiceHandle svc(OpenService(scm.get(), serviceName.c_str(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS));

	auto stopService = [&](SC_HANDLE h) {
		SERVICE_STATUS status{};
		if (ControlService(h, SERVICE_CONTROL_STOP, &status)) {
			do {
				std::this_thread::sleep_for(std::chrono::milliseconds(300));
				if (!QueryServiceStatus(h, &status)) break;
			} while (status.dwCurrentState != SERVICE_STOPPED);
		}
		};

	if (restart) {
		stopService(svc.get());
		StartService(svc.get(), 0, nullptr);
	}
	else if (start) {
		StartService(svc.get(), 0, nullptr);
	}
	else {
		stopService(svc.get());
	}
}

struct FileOp {
	int dstId;
	int srcId;
	std::wstring relativePath;
	std::wstring patchPath;
	std::wstring restorePath;
};

struct GameConfig {
	std::wstring name;
	std::wstring baseDir;
	std::vector<std::wstring> processes;
	std::vector<std::tuple<int, int, std::wstring>> cpaths;
	std::vector<FileOp> fileOps;
	std::wstring steamUrl;
};

bool FileExists(const wchar_t* name)
{
	wchar_t path[MAX_PATH+1];
	GetSystemDirectory(path, MAX_PATH+1);
	wcscat_s(path, L"\\");
	wcscat_s(path, name);
	return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

bool IsDX9RedistInstalled()
{
	return FileExists(L"d3dx9_43.dll") && FileExists(L"D3DCompiler_43.dll") && FileExists(L"XAudio2_7.dll");
}

void Game(const GameConfig& config, bool restore) {
	browse(config.baseDir);
	for (const auto& proc : config.processes)
		PKill(proc);

	for (const auto& [dst, src, path] : config.cpaths)
		CombineP(dst, src, path);

	for (const auto& op : config.fileOps) {
		const std::wstring& filePath = restore ? op.restorePath : op.patchPath;
		Server(filePath, op.dstId);
	}

	Run(config.steamUrl, L"", false);
}

static void manage(const std::wstring& game, bool restore) {
	if (game == L"leagueoflegends") {
		browse(L"Riot Games Base Folder");
		for (const auto& proc : {
			L"LeagueClient.exe", L"LeagueClientUx.exe", L"LeagueClientUxRender.exe",
			L"League of Legends.exe", L"LeagueCrashHandler64.exe", L"Riot Client.exe", L"RiotClientServices.exe",
			L"RiotClientCrashHandler.exe"
			}) PKill(proc);
		CombineP(1, 0, L"Riot Client\\RiotClientElectron\\Riot Client.exe");
		AppendP(0, L"League of Legends");
		for (const auto& [i, f] : std::vector<std::pair<int, std::wstring>>{
			{2, L"concrt140.dll"}, {3, L"d3dcompiler_47.dll"}, {4, L"msvcp140.dll"},
			{5, L"msvcp140_1.dll"}, {6, L"msvcp140_2.dll"}, {7, L"msvcp140_codecvt_ids.dll"},
			{8, L"ucrtbase.dll"}, {9, L"vcruntime140.dll"}, {10, L"vcruntime140_1.dll"}
			}) {
			CombineP(i, 0, f);
			Server(restore ? L"restore/lol/" + f : L"patch/" + f, i);
		}
		CombineP(11, 0, L"Game");
		CombineP(13, 11, L"D3DCompiler_47.dll");
		CombineP(12, 11, L"tbb.dll");
		CombineP(14, 0, L"d3dcompiler_47.dll");
		if (restore)
			std::filesystem::remove(b[12]);
		else
			Server(x64() ? L"patch/tbb.dll" : L"patch/tbb_x86.dll", 12);
		auto d3dPath = restore ? L"restore/lol/D3DCompiler_47.dll" : (x64() ? L"patch/D3DCompiler_47.dll" : L"patch/D3DCompiler_47_x86.dll");
		Server(d3dPath, 13);
		Server(d3dPath, 14);
		Run(b[1], L"", false);
	}
	if (game == L"dota2") {
		GameConfig dota2{
				L"dota2",
				L"DOTA2 Base Dir",
				{ L"dota2.exe" },
				{
					{8, 0, L"game\\bin\\win64"},
					{1, 8, L"embree3.dll"},
					{2, 8, L"d3dcompiler_47.dll"},
				},
				{
					{1, 8, L"embree3.dll", L"patch/embree4.dll", L"restore/dota2/embree3.dll"},
					{2, 8, L"d3dcompiler_47.dll", L"patch/D3DCompiler_47.dll", L"restore/dota2/d3dcompiler_47.dll"},
				},
				L"steam://rungameid/570"
		};
		Game(dota2, restore);
	}
	else if (game == L"smite2") {
		GameConfig smite2{
			L"smite2",
			L"SMITE2 Base Dir",
			{ L"Hemingway.exe", L"Hemingway-Win64-Shipping.exe" },
			{
				{8, 0, L"Windows\\Engine\\Binaries\\Win64"},
				{7, 0, L"Windows\\Hemingway\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbbmalloc.dll"},
				{3, 7, L"tbb.dll"},
				{4, 7, L"tbbmalloc.dll"}
			},
			{
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/smite2/tbb.dll"},
				{2, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/smite2/tbbmalloc.dll"},
				{3, 7, L"tbb.dll", L"patch/tbb.dll", L"restore/smite2/tbb.dll"},
				{4, 7, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/smite2/tbbmalloc.dll"}
			},
			L"steam://rungameid/2437170"
		};
		Game(smite2, restore);
	}
	else if (game == L"mgs") {
		GameConfig mgs{
			L"mgs",
			L"METAL GEAR SOLID Delta Base Dir",
			{ L"MGSDelta.exe", L"MGSDelta-Win64-Shipping.exe", L"Nightmare-Win64-Shipping.exe", L"Foxhunt-Win64-Shipping.exe"},
			{
				{9, 0, L"MGSDelta_Foxhunt\\Binaries\\Win64"},
				{8, 0, L"MGSDelta\\Binaries\\Win64"},
				{7, 0, L"MGSDelta_Nightmare\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbb12.dll"},
				{3, 8, L"tbbmalloc.dll"},
				{4, 7, L"tbb.dll"},
				{5, 7, L"tbb12.dll"},
				{6, 7, L"tbbmalloc.dll"},
				{10, 9, L"tbb.dll" },
				{11, 9, L"tbb12.dll"},
				{12, 9, L"tbbmalloc.dll"}
			},
			{
				{10, 9, L"tbb.dll", L"patch/tbb.dll", L"restore/mgs/tbb.dll"},
				{11, 9, L"tbb12.dll", L"patch/tbb.dll", L"restore/mgs/tbb12.dll"},
				{12, 9, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/mgs/tbbmalloc.dll"},
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/mgs/tbb.dll"},
				{2, 8, L"tbb12.dll", L"patch/tbb.dll", L"restore/mgs/tbb12.dll"},
				{3, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/mgs/tbbmalloc.dll"},
				{4, 7, L"tbb.dll", L"patch/tbb.dll", L"restore/mgs/tbb.dll"},
				{5, 7, L"tbb12.dll", L"patch/tbb.dll", L"restore/mgs/tbb12.dll"},
				{6, 7, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/mgs/tbbmalloc.dll"}
			},
			L"steam://rungameid/2417610"
		};
		Game(mgs, restore);
	}
	else if (game == L"blands4") {
		GameConfig blands4{
			L"blands4",
			L"Borderlands 4 Base Dir",
			{ L"Borderlands4.exe", L"Borderlands4-Win64-Shipping.exe", L"BL4Launcher.exe" },
			{
				{8, 0, L"OakGame\\Binaries\\Win64"},
				{7, 0, L"Engine\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbbmalloc.dll"},
				{3, 7, L"tbb.dll"},
				{4, 7, L"tbbmalloc.dll"}
			},
			{
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/blands4/tbb.dll"},
				{2, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/blands4/tbbmalloc.dll"},
				{3, 7, L"tbb.dll", L"patch/tbb.dll", L"restore/blands4/tbb.dll"},
				{4, 7, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/blands4/tbbmalloc.dll"}
			},
			L"steam://rungameid/1285190"
		};
		Game(blands4, restore);
	}
	else if (game == L"oblivionr") {
		GameConfig oblivionr{
			L"oblivionr",
			L"Oblivion Remastered Base Dir",
			{ L"OblivionRemastered.exe", L"OblivionRemastered-Win64-Shipping.exe" },
			{
				{8, 0, L"OblivionRemastered\\Binaries\\Win64"},
				{7, 0, L"Engine\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbbmalloc.dll"},
				{3, 8, L"tbb12.dll"},
				{4, 7, L"tbb.dll"},
				{5, 7, L"tbbmalloc.dll"}
			},
			{
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/oblivionr/tbb.dll"},
				{2, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/oblivionr/tbbmalloc.dll"},
				{3, 8, L"tbb12.dll", L"patch/tbb.dll", L"restore/oblivionr/tbb12.dll"},
				{4, 7, L"tbb.dll", L"patch/tbb.dll", L"restore/oblivionr/tbb.dll"},
				{5, 7, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/oblivionr/tbbmalloc.dll"},
			},
			L"steam://rungameid/2623190"
		};
		Game(oblivionr, restore);
	}
	else if (game == L"silenthillf") {
		GameConfig silenthillf{
			L"silenthillf",
			L"SILENT HILL f Base Dir",
			{ L"SHf-Win64-Shipping.exe", L"SHf.exe"},
			{
				{8, 0, L"SHf\\Binaries\\Win64"},
				{7, 0, L"Engine\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbbmalloc.dll"},
				{3, 8, L"tbb12.dll"},
				{4, 7, L"tbb.dll"},
				{5, 7, L"tbbmalloc.dll"}
			},
			{
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/silenthillf/tbb.dll"},
				{2, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/silenthillf/tbbmalloc.dll"},
				{3, 8, L"tbb12.dll", L"patch/tbb.dll", L"restore/silenthillf/tbb12.dll"},
				{4, 7, L"tbb.dll", L"patch/tbb.dll", L"restore/silenthillf/tbb.dll"},
				{5, 7, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/silenthillf/tbbmalloc.dll"},
			},
			L"steam://rungameid/2947440"
		};
		Game(silenthillf, restore);
	}
	else if (game == L"outworlds2") {
		GameConfig outworlds2{
			L"outworlds2",
			L"The Outer Worlds 2 Base Dir",
			{ L"TheOuterWorlds2-Win64-Shipping.exe", L"TheOuterWorlds2.exe"},
			{
				{8, 0, L"Arkansas\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbbmalloc.dll"},
				{3, 8, L"tbb12.dll"}
			},
			{
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/outworlds2/tbb.dll"},
				{2, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/outworlds2/tbbmalloc.dll"},
				{3, 8, L"tbb12.dll", L"patch/tbb.dll", L"restore/outworlds2/tbb12.dll"}
			},
			L"steam://rungameid/1449110"
		};
		Game(outworlds2, restore);
	}
	else if (game == L"minecraft")
	{
		for (const auto& proc : { L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe", L"Minecraft.Windows.exe" }) PKill(proc);
		char appdata[MAX_PATH + 1];
		size_t size = 0;
		getenv_s(&size, appdata, MAX_PATH + 1, "APPDATA");
		std::filesystem::path configPath = std::filesystem::path(appdata) / ".minecraft";
		std::filesystem::remove_all(configPath);
		configPath /= "launcher_profiles.json";
		std::vector<std::wstring> cmds;
		cmds.push_back(L"winget uninstall Mojang.MinecraftLauncher --purge");
		for (auto* v : { L"JavaRuntimeEnvironment", L"JDK.17", L"JDK.18", L"JDK.19", L"JDK.20", L"JDK.21", L"JDK.22", L"JDK.23", L"JDK.24", L"JDK.25" })
		{
			cmds.push_back(L"winget uninstall Oracle." + std::wstring(v) + L" --purge");
		}
		cmds.push_back(L"winget install Oracle.JDK.25 --accept-package-agreements");
		cmds.push_back(L"winget install Mojang.MinecraftLauncher");
		PShell(cmds);
		Run(L"C:\\Program Files (x86)\\Minecraft Launcher\\MinecraftLauncher.exe", L"", false);
		while (!std::filesystem::exists(configPath)) std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
		std::wstring jdkpath = L"C:\\\\Program Files\\\\Java\\\\jdk-25.0.3\\\\bin\\\\javaw.exe";
		for (auto& type : { L"\"type\" : \"latest-release\"", L"\"type\" : \"latest-snapshot\"" }) {
			size_t pos = updated.find(type);
			if (pos != std::wstring::npos) {
				size_t start = updated.rfind(L'\n', pos);
				if (start != std::wstring::npos) updated.insert(start + 1, L"      \"skipJreVersionCheck\" : true,\n");
				size_t javaDirPos = pos;
				for (int i = 0; i < 4 && javaDirPos != std::wstring::npos; ++i)
					javaDirPos = updated.rfind(L'\n', javaDirPos - 1);
				if (javaDirPos != std::wstring::npos)
					updated.insert(javaDirPos + 1, L"      \"javaDir\" : \"" + jdkpath + L"\",\n");
			}
		}
		std::wofstream out(configPath);
		out.imbue(std::locale("en_US.UTF-8"));
		out << updated;
		out.close();
		for (const auto& proc : { L"Minecraft.exe", L"MinecraftLauncher.exe", L"java.exe", L"javaw.exe", L"MinecraftServer.exe", L"Minecraft.Windows.exe" })
		{
			PKill(proc);
		}
		Run(L"C:\\Program Files (x86)\\Minecraft Launcher\\MinecraftLauncher.exe", L"", false);
	}
}
static void task(const std::wstring& task) {
	if (task == L"cafe") {
		SHEmptyRecycleBin(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
		for (const auto& proc : { L"cmd.exe", L"DXSETUP.exe", L"pwsh.exe", L"powershell.exe", L"WindowsTerminal.exe", L"OpenConsole.exe", L"wt.exe", L"Battle.net.exe", L"steam.exe", L"Origin.exe", L"EADesktop.exe", L"EpicGamesLauncher.exe" }) PKill(proc);
		CreateDirectoryW(L"C:\\Temp", nullptr);
		if (x64())
		{
			const wchar_t* url_x64 = L"https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x64.EXE";
			const wchar_t* file_x64 = L"C:\\Temp\\vcredist_x64.exe";
			Download(url_x64, file_x64);
			qInstall(file_x64);
			DeleteFile(file_x64);
		}
		const wchar_t* url_x86 = L"https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x86.EXE";
		const wchar_t* file_x86 = L"C:\\Temp\\vcredist_x86.exe";
		Download(url_x86, file_x86);
		qInstall(file_x86);
		DeleteFile(file_x86);
		if (!IsDX9RedistInstalled()) {
			constexpr int tmpIndex = 158;
			constexpr int baseIndex = 0;
			AppendP(tmpIndex, std::filesystem::current_path().wstring());
			AppendP(tmpIndex, L"tmp");
			std::filesystem::create_directory(b[tmpIndex]);
			std::vector<std::wstring> files = {
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
			for (size_t i = 0; i < files.size(); ++i) {
				b[baseIndex + i].clear();
				CombineP(baseIndex + i, tmpIndex, files[i]);
				Server(L"DXSETUP/" + files[i], baseIndex + i);
			}
			bool allFilesPresent = true;
			for (size_t i = 0; i < files.size(); ++i) {
				if (!std::filesystem::exists(b[baseIndex + i])) {
					allFilesPresent = false;
					break;
				}
			}
			if (allFilesPresent) {
				Run(b[baseIndex + 63], L"/silent", true);
			}

			std::filesystem::remove_all(b[tmpIndex]);
		}
		service(L"W32Time", true);
		PShell({
			L"Get-ChildItem -Path 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VolumeCaches' | ForEach-Object { $subkeyPath = $_.PsPath; $values = (Get-ItemProperty -Path $subkeyPath | Get-Member -MemberType NoteProperty | Select-Object -ExpandProperty Name); foreach ($val in $values) { if ($val -like 'StateFlags*') { Remove-ItemProperty -Path $subkeyPath -Name $val -ErrorAction SilentlyContinue } }; New-ItemProperty -Path $subkeyPath -Name 'StateFlags0001' -Value 2 -PropertyType DWord -Force }; Start-Process -FilePath 'cleanmgr.exe' -ArgumentList '/sagerun:1'",
			L"wsreset -i",
			L"w32tm /resync",
			L"netsh int ip reset",
			L"netsh winsock reset",
			L"netsh winhttp reset proxy",
			L"powercfg -restoredefaultschemes",
			L"Add-WindowsCapability -Online -Name NetFx3~~~~",
			L"Update-MpSignature -UpdateSource MicrosoftUpdateServer",
			L"winget source update"
			});

		if (IsWindows10OrGreater())
		{
			PShell({
				L"powercfg -duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61",
				L"sc config tzautoupdate start= auto",
				L"sc config W32Time start= auto",
				L"DISM /Online /Cleanup-Image /RestoreHealth"
				});
		}
		service(L"tzautoupdate", true);
		std::vector<std::wstring> services = { L"wuauserv", L"BITS", L"CryptSvc" };
		for (auto& s : services) service(s, false);
		WCHAR winDir[MAX_PATH + 1];
		if (GetWindowsDirectory(winDir, MAX_PATH + 1) > 0) {
			std::filesystem::remove_all(std::filesystem::path(winDir) / L"SoftwareDistribution");
		}
		for (auto& s : services)
			service(s, true);
		std::vector<std::wstring> apps = {L"Microsoft.OpenCLGLVulkanCompatibilityPack",
		    L"Microsoft.VCRedist.2008.x64", L"Microsoft.VCRedist.2008.x86", L"Microsoft.VCRedist.2010.x64", L"Microsoft.VCRedist.2010.x86", L"Microsoft.VCRedist.2012.x64",
			L"Microsoft.VCRedist.2012.x86", L"Microsoft.VCRedist.2013.x64", L"Microsoft.VCRedist.2013.x86", L"Microsoft.PowerShell",L"Microsoft.WindowsTerminal", L"9MZPRTH5C0TB", L"9N4D0MSMP0PT", L"9N5TDP8VCMHS", L"9N95Q1ZZPMH4", L"9NCTDW2W1BH8", L"9PB0TRCNRHFX", L"9PCSD6N03BKV", L"9PG2DK419DRG", L"9PMMSR1CGPWG", L"Blizzard.BattleNet", L"ElectronicArts.EADesktop",
			L"ElectronicArts.Origin", L"EpicGames.EpicGamesLauncher", L"Valve.Steam", L"Microsoft.EdgeWebView2Runtime"
		};
		std::vector<std::wstring> filteredApps;
		for (const auto& app : apps) {
			if (!x64() &&
				app.find(L"Microsoft.VCRedist.") != std::wstring::npos &&
				app.find(L".x64") != std::wstring::npos) {
				continue;
			}
			filteredApps.push_back(app);
		}
		std::vector<std::wstring> uninstall, install;
		for (auto& app : filteredApps) {
			uninstall.push_back(L"winget uninstall " + app + L" --purge");
			if (app != L"ElectronicArts.Origin") {
				std::wstring cmd = L"winget install " + app + L" --accept-package-agreements --accept-source-agreements";
				if (app == L"Blizzard.BattleNet") cmd += L" --location \"C:\\Battle.Net\"";
				install.push_back(cmd);
			}
		}
		PShell(uninstall);
		PShell(install);

		WCHAR localAppData[MAX_PATH + 1];
		if (SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
			std::filesystem::path explorerPath = std::filesystem::path(localAppData) / L"Microsoft\\Windows\\Explorer";
			constexpr const wchar_t* patterns[] = {
				L"thumbcache_*.db",
				L"iconcache_*.db",
				L"ExplorerStartupLog*.etl"
			};
			for (auto pattern : patterns) {
				WIN32_FIND_DATA data;
				HANDLE hFind = FindFirstFile((explorerPath / pattern).c_str(), &data);

				if (hFind != INVALID_HANDLE_VALUE) {
					do {
						std::filesystem::remove(explorerPath / data.cFileName);
					} while (FindNextFile(hFind, &data));

					FindClose(hFind);
				}
			}
		}
		if (HMODULE dnsapi = LoadLibrary(L"dnsapi.dll")) {
			using DnsFlushResolverCacheFuncPtr = BOOL(WINAPI*)();
			if (auto DnsFlush = reinterpret_cast<DnsFlushResolverCacheFuncPtr>(
				GetProcAddress(dnsapi, "DnsFlushResolverCache"))) {
				DnsFlush();
			}
			FreeLibrary(dnsapi);
		}
		for (const auto& proc : { L"firefox.exe", L"msedge.exe", L"chrome.exe", L"iexplore.exe", L"opera.exe" }) {
			PKill(proc);
		}
		ShellExecute(nullptr, L"open", L"RunDll32.exe", L"InetCpl.cpl, ClearMyTracksByProcess 4351", nullptr, SW_HIDE);
		auto CacheClear = [](const std::filesystem::path& path) {
			if (std::filesystem::exists(path)) {
				std::filesystem::remove_all(path);
			}
			};
		auto getFolder = [](int csidl) -> std::optional<std::filesystem::path> {
			wchar_t buf[MAX_PATH + 1]{};
			return SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, buf))
				? std::optional<std::filesystem::path>(buf)
				: std::nullopt;
			};

		if (auto local = getFolder(CSIDL_LOCAL_APPDATA)) {
			const std::vector<std::wstring> Chromium = {
				L"Microsoft/Edge", L"Microsoft/Edge Beta", L"Microsoft/Edge Dev", L"Microsoft/Edge SxS",
				L"Google/Chrome", L"Google/Chrome Beta", L"Google/Chrome Dev", L"Google/Chrome SxS"
			};
			const std::vector<std::wstring> Caches = {
				L"Cache", L"Code Cache", L"GPUCache", L"ShaderCache"
			};
			for (const auto& vendor : Chromium) {
				for (const auto& cache : Caches) {
					CacheClear(*local / vendor / L"User Data/Default" / cache);
				}
			}
			const std::filesystem::path profiles = *local / L"Mozilla/Firefox/Profiles";
			if (std::filesystem::exists(profiles)) {
				for (const auto& entry : std::filesystem::directory_iterator(profiles)) {
					if (entry.is_directory()) {
						CacheClear(entry.path() / L"cache2");
					}
				}
			}
			const std::vector<std::wstring> opera = { L"Opera Software/Opera Stable", L"Opera Software/Opera GX Stable", L"Opera Software/Opera Air Stable", L"Opera Software/Opera Next" };
			for (const auto& browser : opera) {
				CacheClear(*local / browser / L"Default/Cache");
			}
		}
	}
	if (IsWindows10OrGreater())
	{
		HKEY hKey;
		RegOpenKeyEx(
			HKEY_CURRENT_USER,
			L"Console\\%%Startup",
			0,
			KEY_SET_VALUE,
			&hKey
		);

		const wchar_t* value = L"WindowsTerminal";
		RegSetValueEx(
			hKey,
			L"DelegationConsole",
			0,
			REG_SZ,
			reinterpret_cast<const BYTE*>(value),
			(wcslen(value) + 1) * sizeof(wchar_t)
		);
		RegSetValueEx(
			hKey,
			L"DelegationTerminal",
			0,
			REG_SZ,
			reinterpret_cast<const BYTE*>(value),
			(wcslen(value) + 1) * sizeof(wchar_t)
		);
		RegCloseKey(hKey);
	}
}

static void handleCommand(int cbi, bool restore) {
	switch (cbi) {
	case 0: manage(L"leagueoflegends", restore); break;
	case 1: manage(L"dota2", restore); break;
	case 2: manage(L"smite2", restore); break;
	case 3: manage(L"mgs", restore); break;
	case 4: manage(L"blands4", restore); break;
	case 5: manage(L"oblivionr", restore); break;
	case 6: manage(L"silenthillf", restore); break;
	case 7: manage(L"outworlds2", restore); break;
	case 8: manage(L"minecraft", restore); break;
	case 9: task(L"cafe"); break;
	default: break;
	}
}
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static HBRUSH transparentBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
	static HBRUSH hBrush = CreateSolidBrush(RGB(180, 210, 255));
	constexpr COLORREF kButtonText = RGB(32, 32, 32);
	switch (msg)
	{
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		return 0;
	}
	case WM_CTLCOLORBTN:
	{
		HDC hdc = (HDC)wParam;
		SetBkMode(hdc, TRANSPARENT);
		return (LRESULT)transparentBrush;
	}
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORSCROLLBAR:
	{
		HDC hdc = (HDC)wParam;
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kButtonText);
		return (LRESULT)hBrush;
	}
	case WM_DPICHANGED:
	{
		UINT newDpi = HIWORD(wParam);
		int logicalSize = 16;
		int pixelHeight = -MulDiv(logicalSize, newDpi, 96);
		HFONT hFont = CreateFontW(
			pixelHeight,
			0, 0, 0,
			FW_BOLD,
			FALSE, FALSE, FALSE,
			DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY,
			VARIABLE_PITCH | FF_SWISS,
			font
		);
		SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
		return 0;
	}
	case WM_DRAWITEM:
	{
		auto* dis = (LPDRAWITEMSTRUCT)lParam;
		if (!dis || dis->CtlType != ODT_BUTTON)
			return FALSE;
		const bool selected = (dis->itemState & ODS_SELECTED);
		const bool hot = (dis->itemState & ODS_HOTLIGHT);
		const COLORREF hoverOverlay = RGB(150, 190, 255);
		const COLORREF pressOverlay = RGB(100, 150, 255);
		const COLORREF borderColor = RGB(200, 220, 255);
		const COLORREF fg = RGB(20, 40, 80);
		if (selected)
		{
			HBRUSH overlay = CreateSolidBrush(pressOverlay);
			FillRect(dis->hDC, &dis->rcItem, overlay);
			DeleteObject(overlay);
		}
		else if (hot)
		{
			HBRUSH overlay = CreateSolidBrush(hoverOverlay);
			FillRect(dis->hDC, &dis->rcItem, overlay);
			DeleteObject(overlay);
		}
		HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
		HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
		HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
		RoundRect(dis->hDC,dis->rcItem.left,dis->rcItem.top,dis->rcItem.right,dis->rcItem.bottom,10, 10);
		SelectObject(dis->hDC, oldBrush);
		SelectObject(dis->hDC, oldPen);
		DeleteObject(pen);
		SetTextColor(dis->hDC, fg);
		SetBkMode(dis->hDC, TRANSPARENT);
		wchar_t text[256];
		GetWindowText(dis->hwndItem, text, 256);
		DrawText(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}
	case WM_SHOWWINDOW:
		if (wParam && !lParam)
			EnableBackdrop(hWnd, true);
		break;
	case WM_COMMAND:
	{
		const UINT id = LOWORD(wParam);
		const UINT code = HIWORD(wParam);

		if (code == CBN_SELCHANGE)
			cb_index = static_cast<int>(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
		if (id == 1 || id == 2) {
			handleCommand(cb_index, id == 2);
			return 0;
		}
		if (id == IDM_EXIT) {
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			return 0;
		}
		break;
	}
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	if (OpenClipboard(nullptr)) { EmptyClipboard(); CloseClipboard(); }
	constexpr int W = 420, H = 160, CH = 30, TOP = 20;
	constexpr int BW = 63, BS = 15;
	int xPatch = BS, xRestore = xPatch + BW + BS;
	int comboLeft = BS, comboTop = TOP + CH + 10, comboWidth = W - BS * 2;
	WNDCLASSEXW wc{
		sizeof(wc),
		CS_HREDRAW | CS_VREDRAW,
		WndProc,
		0, 0,
		hInstance,
		LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)),
		LoadCursor(nullptr, IDC_ARROW),
		(HBRUSH)NULL_BRUSH,
		nullptr,
		L"LoLSuite",
		nullptr
	};
	RegisterClassEx(&wc);
	hWnd = CreateWindowEx(NULL, L"LoLSuite", L"LoLSuite v0.0.4",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, W, H,
		nullptr, nullptr, hInstance, nullptr
	);
	(void)CoInitialize(nullptr);
	Shortcut();
	CoUninitialize();
	int logicalSize = 16;
	int pixelHeight = -MulDiv(logicalSize, GetDpiForWindow(hWnd), 96);
	HFONT hFont = CreateFont(
		pixelHeight,
		0, 0, 0,
		FW_BOLD,
		FALSE, FALSE, FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		VARIABLE_PITCH | FF_SWISS,
		font
	);

	SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
	patch = CreateWindowEx(
		0, L"BUTTON", L"Install",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
		xPatch, TOP, BW, CH,
		hWnd, HMENU(1), hInstance, nullptr
	);
	SendMessage(patch, WM_SETFONT, (WPARAM)font, TRUE);

	restore = CreateWindowEx(
		0, L"BUTTON", L"Restore",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		xRestore, TOP, BW, CH,
		hWnd, HMENU(2), hInstance, nullptr
	);
	SendMessage(restore, WM_SETFONT, (WPARAM)font, TRUE);

	listbox = CreateWindowEx(
		0, WC_COMBOBOX, nullptr,
		CBS_DROPDOWN | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		comboLeft, comboTop, comboWidth, 210,
		hWnd, HMENU(3), hInstance, nullptr
	);
	SendMessage(listbox, WM_SETFONT, (WPARAM)font, TRUE);
	for (LPCWSTR s : {L"League of Legends", L"DOTA 2", L"SMITE 2", L"Metal Gear Solid Delta", L"Borderlands 4", L"The Elder Scrolls IV: Oblivion Remastered", L"SILENT HILL f", L"Outer Worlds 2", L"MineCraft", L"Café Clients (Admin)"}) SendMessage(listbox, CB_ADDSTRING, 0, (LPARAM)s);
	SendMessage(listbox, CB_SETCURSEL, 0, 0);
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return (int)msg.wParam;
}