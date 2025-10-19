﻿#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShObjIdl_core.h>
#include <Shlobj_core.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <urlmon.h>
#include <wininet.h>
#include <filesystem>
#include <vector>
#include <fstream>
#include "resource.h"

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

int cb_index = 0;
std::vector<std::wstring> b(158);
HWND hWnd, hwndPatch, hwndRestore, combo;
static std::wstring JPath(const std::wstring& base, const std::wstring& addition) {
	return (std::filesystem::path(base) / addition).wstring();
}

static void APath(int index, const std::wstring& addition) {
	b[index] = JPath(b[index], addition);
}

static void CPath(int destIndex, int srcIndex, const std::wstring& addition) {
	b[destIndex] = JPath(b[srcIndex], addition);
}

static void dl(const std::wstring& url, int idx) {
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

static void ProcKill(const std::wstring& name) {
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

static bool x64() {
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

std::wstring browse(const std::wstring& pathLabel) {
	std::wstring iniPath = (std::filesystem::current_path() / L"LoLSuite.ini").wstring();
	wchar_t savedPath[MAX_PATH] = {};
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

static void serviceman(const std::wstring& serviceName, bool start, bool restart = false) {
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
				Sleep(500);
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
				Sleep(500);
				if (!QueryServiceStatus(svc.get(), &status)) break;
			}
		}
	}
}

static void manageGame(const std::wstring& game, bool restore) {
	if (game == L"leagueoflegends") {
		browse(L"<drive>:/Riot Games Base Folder");
		for (const auto& proc : {
			L"LeagueClient.exe", L"LeagueClientUx.exe", L"LeagueClientUxRender.exe",
			L"League of Legends.exe", L"Riot Client.exe", L"RiotClientServices.exe",
			L"RiotClientCrashHandler.exe", L"LeagueCrashHandler64.exe"
			}) ProcKill(proc);
		CPath(1, 0, L"Riot Client\\RiotClientElectron\\Riot Client.exe");
		APath(0, L"League of Legends");
		for (const auto& [i, f] : std::vector<std::pair<int, std::wstring>>{
			{2, L"concrt140.dll"}, {3, L"d3dcompiler_47.dll"}, {4, L"msvcp140.dll"},
			{5, L"msvcp140_1.dll"}, {6, L"msvcp140_2.dll"}, {7, L"msvcp140_codecvt_ids.dll"},
			{8, L"ucrtbase.dll"}, {9, L"vcruntime140.dll"}, {10, L"vcruntime140_1.dll"}
			}) {
			CPath(i, 0, f);
			dl(restore ? L"restore/lol/" + f : L"patch/" + f, i);
		}
		CPath(11, 0, L"Game");
		CPath(13, 11, L"D3DCompiler_47.dll");
		CPath(12, 11, L"tbb.dll");
		CPath(14, 0, L"d3dcompiler_47.dll");
		if (restore)
			std::filesystem::remove(b[12]);
		else
			dl(x64() ? L"patch/tbb.dll" : L"patch/tbb_x86.dll", 12);
		auto d3dPath = restore ? L"restore/lol/D3DCompiler_47.dll" :
			(x64() ? L"patch/D3DCompiler_47.dll" : L"patch/D3DCompiler_47_x86.dll");
		dl(d3dPath, 13);
		dl(d3dPath, 14);
		Run(b[1], L"", false);
	}
	else if (game == L"dota2") {
		browse(L"Steam DOTA2 Base Dir");
		ProcKill(L"dota2.exe");
		APath(0, L"game\\bin\\win64");
		CPath(1, 0, L"embree3.dll");
		CPath(2, 0, L"d3dcompiler_47.dll");
		dl(restore ? L"restore/dota2/embree3.dll" : L"patch/embree4.dll", 1);
		dl(restore ? L"restore/dota2/d3dcompiler_47.dll" : L"patch/D3DCompiler_47.dll", 2);
		Run(L"steam://rungameid/570//-high -dx11 -fullscreen/", L"", false);
	}
	else if (game == L"smite2") {
		browse(L"SMITE2 Base Dir");
		APath(0, L"Windows");
		for (const auto& proc : { L"Hemingway.exe", L"Hemingway-Win64-Shipping.exe" })
			ProcKill(proc);
		CPath(8, 0, L"Engine\\Binaries\\Win64");
		CPath(7, 0, L"Hemingway\\Binaries\\Win64");
		CPath(1, 8, L"tbb.dll");
		CPath(2, 8, L"tbbmalloc.dll");
		CPath(3, 7, L"tbb.dll");
		CPath(4, 7, L"tbbmalloc.dll");
		dl(restore ? L"restore/smite2/tbb.dll" : L"patch/tbb.dll", 1);
		dl(restore ? L"restore/smite2/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 2);
		dl(restore ? L"restore/smite2/tbb.dll" : L"patch/tbb.dll", 3);
		dl(restore ? L"restore/smite2/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 4);
		Run(L"steam://rungameid/2437170", L"", false);
	}
	else if (game == L"mgsΔ") {
		browse(L"METAL GEAR SOLID Delta Base Dir");
		for (const auto& proc : { L"MGSDelta.exe", L"MGSDelta-Win64-Shipping.exe", L"Nightmare-Win64-Shipping.exe" })
			ProcKill(proc);
		CPath(8, 0, L"MGSDelta\\Binaries\\Win64");
		CPath(7, 0, L"MGSDelta_Nightmare\\Binaries\\Win64");
		CPath(1, 8, L"tbb.dll");
		CPath(2, 8, L"tbb12.dll");
		CPath(3, 8, L"tbbmalloc.dll");
		CPath(4, 7, L"tbb.dll");
		CPath(5, 7, L"tbb12.dll");
		CPath(6, 7, L"tbbmalloc.dll");
		dl(restore ? L"restore/mgs/tbb.dll" : L"patch/tbb.dll", 1);
		dl(restore ? L"restore/mgs/tbb12.dll" : L"patch/tbb.dll", 2);
		dl(restore ? L"restore/mgs/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 3);
		dl(restore ? L"restore/mgs/tbb.dll" : L"patch/tbb.dll", 4);
		dl(restore ? L"restore/mgs/tbb12.dll" : L"patch/tbb.dll", 5);
		dl(restore ? L"restore/mgs/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 6);
		Run(L"steam://rungameid/2417610", L"", false);
	}
	else if (game == L"blands4") {
		browse(L"Borderlands 4 Base Dir");
		for (const auto& proc : { L"Borderlands4.exe", L"Borderlands4-Win64-Shipping.exe", L"BL4Launcher.exe" })
			ProcKill(proc);
		CPath(8, 0, L"OakGame\\Binaries\\Win64");
		CPath(1, 8, L"tbb.dll");
		CPath(2, 8, L"tbbmalloc.dll");
		dl(restore ? L"restore/blands4/tbb.dll" : L"patch/tbb.dll", 1);
		dl(restore ? L"restore/blands4//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 2);

		CPath(7, 0, L"Engine\\Binaries\\Win64");
		CPath(3, 7, L"tbb.dll");
		CPath(4, 7, L"tbbmalloc.dll");
		dl(restore ? L"restore/blands4/tbb.dll" : L"patch/tbb.dll", 3);
		dl(restore ? L"restore/blands4//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 4);

		Run(L"steam://rungameid/1285190", L"", false);
	}
	else if (game == L"oblivionr") {
		browse(L"Oblivion Remastered Base Dir");
		for (const auto& proc : { L"OblivionRemastered.exe", L"OblivionRemastered-Win64-Shipping.exe" })
			ProcKill(proc);

		CPath(7, 0, L"Engine\\Binaries\\Win64");
		CPath(3, 7, L"tbb.dll");
		CPath(4, 7, L"tbbmalloc.dll");
		dl(restore ? L"restore/oblivionr/tbb.dll" : L"patch/tbb.dll", 3);
		dl(restore ? L"restore/oblivionr//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 4);

		CPath(8, 0, L"OblivionRemastered\\Binaries\\Win64");
		CPath(5, 8, L"tbb.dll");
		CPath(6, 8, L"tbbmalloc.dll");
		CPath(9, 8, L"tbb12.dll");
		dl(restore ? L"restore/oblivionr/tbb.dll" : L"patch/tbb.dll", 5);
		dl(restore ? L"restore/oblivionr//tbbmalloc.dll" : L"patch/tbbmalloc.dll", 6);
		dl(restore ? L"restore/oblivionr/tbb12.dll" : L"patch/tbb.dll", 9);

		Run(L"steam://rungameid/2623190", L"", false);
	}
}

static void manageTask(const std::wstring& task) {
	if (task == L"cafe") {
		serviceman(L"W32Time", false, true);
		for (const auto& proc : {
			L"cmd.exe", L"pwsh.exe", L"powershell.exe", L"WindowsTerminal.exe", L"OpenConsole.exe", L"wt.exe",
			L"Battle.net.exe", L"steam.exe", L"Origin.exe", L"EADesktop.exe", L"EpicGamesLauncher.exe",
			L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe",
			L"Minecraft.Windows.exe"
			}) ProcKill(proc);
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
		for (auto& s : services) serviceman(s, false);
		WCHAR winDir[MAX_PATH + 1];
		if (GetWindowsDirectory(winDir, MAX_PATH + 1)) {
			std::filesystem::remove_all(std::filesystem::path(winDir) / L"SoftwareDistribution");
		}
		for (auto& s : services) serviceman(s, true);
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
		while (!std::filesystem::exists(configPath)) Sleep(100);
		for (const auto& proc : {
			L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe", L"Minecraft.Windows.exe"
			}) ProcKill(proc);
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
		if (HMODULE dnsapi = LoadLibrary(L"dnsapi.dll")) {
			using DnsFlushResolverCacheFuncPtr = BOOL(WINAPI*)();
			if (auto DnsFlush = reinterpret_cast<DnsFlushResolverCacheFuncPtr>(
				GetProcAddress(dnsapi, "DnsFlushResolverCache"))) {
				DnsFlush();
			}
			FreeLibrary(dnsapi);
		}

		for (const auto& proc : { L"firefox.exe", L"msedge.exe", L"chrome.exe", L"iexplore.exe" }) {
			ProcKill(proc);
		}

		ShellExecuteW(nullptr, L"open", L"RunDll32.exe",
			L"InetCpl.cpl, ClearMyTracksByProcess 4351",
			nullptr, SW_HIDE);

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

		SHEmptyRecycleBin(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
	}
}

static void handleCommand(int cb, bool flag) {
	switch (cb) {
	case 0: manageGame(L"leagueoflegends", flag); break;
	case 1: manageGame(L"dota2", flag); break;
	case 2: manageGame(L"smite2", flag); break;
	case 3: manageGame(L"mgsΔ", flag); break;
	case 4: manageGame(L"blands4", flag); break;
	case 5: manageGame(L"oblivionr", flag); break;
	case 6: manageTask(L"cafe"); break;
	case 7: manageTask(L"clear_caches"); break;
	default: break;
	}
}
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	constexpr COLORREF kTextColor = RGB(255, 255, 255);
	constexpr COLORREF kButtonText = RGB(200, 200, 200);
	constexpr COLORREF kBackground = RGB(30, 30, 30);
	static HBRUSH hBrush = CreateSolidBrush(kBackground);

	switch (msg) {
	case WM_COMMAND: {
		UINT id = LOWORD(wParam), code = HIWORD(wParam);
		if (code == CBN_SELCHANGE)
			cb_index = SendMessage(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);

		if (id == 1 || id == 2) {
			handleCommand(cb_index, id == 2);
			return 0;
		}
		if (id == IDM_EXIT) {
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
	case WM_CTLCOLORSCROLLBAR: {
		HDC hdc = reinterpret_cast<HDC>(wParam);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kTextColor);
		return reinterpret_cast<INT_PTR>(hBrush);
	}

	case WM_DRAWITEM: {
		auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
		if (dis && dis->CtlType == ODT_BUTTON) {
			COLORREF bg = (dis->itemState & ODS_SELECTED) ? RGB(0, 120, 215) : kBackground;
			COLORREF fg = (dis->itemState & ODS_SELECTED) ? RGB(255, 255, 255) : kButtonText;

			// Fill background
			HBRUSH brush = CreateSolidBrush(bg);
			FillRect(dis->hDC, &dis->rcItem, brush);
			DeleteObject(brush);

			// Draw white rounded outline
			HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
			HGDIOBJ oldPen = SelectObject(dis->hDC, hPen);
			HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
			RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 10, 10);
			SelectObject(dis->hDC, oldBrush);
			SelectObject(dis->hDC, oldPen);
			DeleteObject(hPen);


			// Draw text
			SetTextColor(dis->hDC, fg);
			SetBkMode(dis->hDC, TRANSPARENT);
			wchar_t text[256] = {};
			GetWindowText(dis->hwndItem, text, _countof(text));
			DrawText(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			return TRUE;
		}
		return FALSE;
	}


	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
}


int wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	if (OpenClipboard(nullptr)) {
		EmptyClipboard();
		CloseClipboard();
	}

	LimitInstance GUID(L"{3025d31f-c76e-435c-a4b48-9d084fa9f5ea}");
	if (LimitInstance::AnotherInstanceRunning()) return 0;

	constexpr int windowWidth = 420;
	constexpr int windowHeight = 160; // or higher depending on ComboBox height
	constexpr int controlHeight = 30;
	constexpr int controlTop = 20;
	constexpr int controlCount = 3;
	constexpr int controlSpacing = 20; // space between controls
	int totalSpacing = controlSpacing * (controlCount + 1); // 4 gaps: left, between, right
	int controlWidth = (windowWidth - totalSpacing) / controlCount;
	constexpr int buttonWidth = 63;
	constexpr int buttonSpacing = 15;
	int xPatch = buttonSpacing;
	int xRestore = xPatch + buttonWidth + buttonSpacing;
	int xCombo = xRestore + controlWidth + controlSpacing;
	int comboLeft = buttonSpacing;
	int comboTop = controlTop + controlHeight + 10; // was + controlSpacing
	int comboWidth = windowWidth - (2 * buttonSpacing);

	WNDCLASSEXW wcex{
			sizeof(wcex), CS_HREDRAW | CS_VREDRAW, WndProc,
			0, 0, hInstance,
			LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)),
			LoadCursor(nullptr, IDC_ARROW),
			CreateSolidBrush(RGB(32, 32, 32)),
			nullptr, L"LoLSuite", nullptr
	};

	RegisterClassEx(&wcex);

	HFONT hFont = CreateFont(
		-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
		L"Segoe UI"
	);

	hWnd = CreateWindowEx(
		WS_EX_LAYERED,
		L"LoLSuite", L"FPS Booster (https://lolsuite.org)",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
		nullptr, nullptr, hInstance, nullptr
	);

	SetLayeredWindowAttributes(hWnd, 0, 229, LWA_ALPHA);

	hwndPatch = CreateWindowEx(
		0, L"BUTTON", L"Patch",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
		xPatch, controlTop, buttonWidth, controlHeight,
		hWnd, HMENU(1), hInstance, nullptr
	);
	SendMessage(hwndPatch, WM_SETFONT, (WPARAM)hFont, TRUE);

	hwndRestore = CreateWindowEx(
		0, L"BUTTON", L"Restore",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		xRestore, controlTop, buttonWidth, controlHeight,
		hWnd, HMENU(2), hInstance, nullptr
	);
	SendMessage(hwndRestore, WM_SETFONT, (WPARAM)hFont, TRUE);

	combo = CreateWindowEx(
		0, WC_COMBOBOX, nullptr,
		CBS_DROPDOWN | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		comboLeft, comboTop, comboWidth, 210,
		hWnd, (HMENU)3, hInstance, nullptr
	);
	SendMessage(combo, WM_SETFONT, (WPARAM)hFont, TRUE);

	std::vector<LPCWSTR> items = {
		L"League of Legends", L"DOTA 2", L"SMITE 2",
		L"Metal Gear Solid Δ", L"Borderlands 4", L"Oblivion : Remastered",
		L"Game Clients", L"Clear Caches"
	};

	for (const auto& item : items) {
		SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
	}
	SendMessage(combo, CB_SETCURSEL, 0, 0);

	bool isDX9Installed = false;
	HMODULE hDX9 = LoadLibrary(L"d3dx9_43.dll");
	if (hDX9) {
		isDX9Installed = true;
		FreeLibrary(hDX9);
	}

	if (!isDX9Installed) {
		constexpr int tmpIndex = 158;
		constexpr int baseIndex = 0;

		APath(tmpIndex, std::filesystem::current_path().wstring());
		APath(tmpIndex, L"tmp");

		std::filesystem::remove_all(b[tmpIndex]);
		std::filesystem::create_directory(b[tmpIndex]);

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

		for (size_t i = 0; i < kFileCount; ++i) {
			b[baseIndex + i].clear();
			CPath(baseIndex + i, tmpIndex, files[i]);
			dl(L"DXSETUP/" + files[i], baseIndex + i);
		}

		bool allFilesPresent = true;
		for (size_t i = 0; i < kFileCount; ++i) {
			if (!std::filesystem::exists(b[baseIndex + i])) {
				allFilesPresent = false;
				break;
			}
		}

		if (allFilesPresent) {
			ProcKill(L"DXSETUP.exe");
			Run(b[baseIndex + 63], L"/silent", true);
		}

		std::filesystem::remove_all(b[tmpIndex]);
	}

	ShowWindow(hWnd, nShowCmd);
	UpdateWindow(hWnd);
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return static_cast<int>(msg.wParam);
}