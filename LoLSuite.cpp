#define WIN32_LEAN_AND_MEAN
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

int cb_index = 0;
std::vector<std::wstring> b(258);
HWND hwndPatch, hwndRestore, combo;

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

std::wstring JoinPath(const std::wstring& base, const std::wstring& addition) {
	return (std::filesystem::path(base) / addition).wstring();
}
void AppendPath(int index, const std::wstring& addition) {
	b[index] = JoinPath(b[index], addition);
}
void CombinePath(int destIndex, int srcIndex, const std::wstring& addition) {
	b[destIndex] = JoinPath(b[srcIndex], addition);
}

void net(const std::wstring& serviceName, bool start, bool restart = false) {
	auto closeHandle = [](SC_HANDLE h) { if (h) CloseServiceHandle(h); };

	std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, decltype(closeHandle)>
		scm(OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS), closeHandle);
	if (!scm) return;

	std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, decltype(closeHandle)>
		svc(OpenService(scm.get(), serviceName.c_str(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS), closeHandle);
	if (!svc) return;

	SERVICE_STATUS status{};

	if (restart) {
		// Stop the service
		if (ControlService(svc.get(), SERVICE_CONTROL_STOP, &status)) {
			while (status.dwCurrentState != SERVICE_STOPPED) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if (!QueryServiceStatus(svc.get(), &status)) break;
			}
		}
		// Start the service again
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

HRESULT folder() {
	b[0].clear();
	WCHAR szFolderPath[MAX_PATH + 1];
	IFileDialog* pfd = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
	if (FAILED(hr) || !pfd) return hr;

	pfd->SetOptions(FOS_PICKFOLDERS);

	if (SUCCEEDED(pfd->Show(nullptr))) {
		IShellItem* psi = nullptr;

		if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
			PWSTR pszPath = nullptr;

			if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
				wcsncpy_s(szFolderPath, ARRAYSIZE(szFolderPath), pszPath, _TRUNCATE);
				b[0] = szFolderPath;
				CoTaskMemFree(pszPath);
			}
			psi->Release();
		}
	}

	pfd->Release();
	return S_OK;
}

void url(const std::wstring& url, int idx) {
	const std::wstring targetUrl = L"https://lolsuite.org/" + url;
	const std::wstring& filePath = b[idx];
	const std::wstring zonePath = filePath + L":Zone.Identifier";

	// Clear cached version
	DeleteUrlCacheEntry(targetUrl.c_str());

	// Download file
	constexpr int maxRetries = 5;
	int attempt = 0;
	HRESULT hr;

	do {
		hr = URLDownloadToFile(nullptr, targetUrl.c_str(), filePath.c_str(), 0, nullptr);
		if (SUCCEEDED(hr)) break;
		++attempt;
		Sleep(500); // brief pause before retry
	} while (attempt < maxRetries);

	// Remove Zone.Identifier ADS if it exists
	if (std::filesystem::exists(zonePath)) {
		std::error_code ec;
		std::filesystem::remove(zonePath, ec);
	}
}

void PowerShell(const std::vector<std::wstring>& commands) {
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

		if (ShellExecuteEx(&sei)) {
			WaitForSingleObject(sei.hProcess, INFINITE);
			CloseHandle(sei.hProcess);
		}
	}
}

void Run(const std::wstring& file, const std::wstring& params, bool wait) {
	SHELLEXECUTEINFO sei{
		.cbSize = sizeof(SHELLEXECUTEINFO),
		.fMask = SEE_MASK_NOCLOSEPROCESS,
		.hwnd = nullptr,
		.lpVerb = L"open",
		.lpFile = file.c_str(),
		.lpParameters = params.c_str(),
		.lpDirectory = nullptr,
		.nShow = SW_SHOWNORMAL,
		.hInstApp = nullptr
	};

	if (ShellExecuteEx(&sei) && wait && sei.hProcess) {
		SetPriorityClass(sei.hProcess, HIGH_PRIORITY_CLASS);
		WaitForSingleObject(sei.hProcess, INFINITE);
		CloseHandle(sei.hProcess);
	}
}

void ExitThread(const std::wstring& name) {
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

bool Is64BitWindows() {
	BOOL isWow64 = FALSE;
	USHORT processMachine = 0, nativeMachine = 0;

	auto fnIsWow64Process2 = reinterpret_cast<BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*)>(
		GetProcAddress(GetModuleHandleW(L"kernel32"), "IsWow64Process2"));

	if (fnIsWow64Process2 &&
		fnIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
		return nativeMachine != IMAGE_FILE_MACHINE_I386;
	}

	auto fnIsWow64Process = reinterpret_cast<BOOL(WINAPI*)(HANDLE, PBOOL)>(
		GetProcAddress(GetModuleHandleW(L"kernel32"), "IsWow64Process"));

	return fnIsWow64Process &&
		fnIsWow64Process(GetCurrentProcess(), &isWow64) &&
		isWow64;
}

void manageGame(const std::wstring& game, bool restore) {
	if (game == L"leagueoflegends") {
		MessageBoxEx(nullptr, L"Select: C:\\Riot Games", L"LoLSuite", MB_OK, 0);
		folder();

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
			DeleteFile(b[55].c_str());
		else
			url(Is64BitWindows() ? L"patch/tbb.dll" : L"patch/tbb_x86.dll", 55);

		auto d3dPath = restore ? L"restore/lol/D3DCompiler_47.dll" :
			(Is64BitWindows() ? L"patch/D3DCompiler_47.dll" : L"patch/D3DCompiler_47_x86.dll");

		url(d3dPath, 53);
		url(d3dPath, 54);

		Run(b[56], L"", false);
	}

	else if (game == L"dota2") {
		MessageBoxEx(nullptr, L"Select: C:\\Program Files (x86)\\Steam\\steamapps\\common\\dota 2 beta", L"LoLSuite", MB_OK, 0);
		folder();

		ExitThread(L"dota2.exe");
		AppendPath(0, L"game\\bin\\win64");
		CombinePath(1, 0, L"embree3.dll");
		CombinePath(2, 0, L"d3dcompiler_47.dll");
		url(restore ? L"restore/dota2/embree3.dll" : L"patch/embree4.dll", 1);
		url(restore ? L"restore/dota2/d3dcompiler_47.dll" : L"patch/D3DCompiler_47.dll", 2);
		Run(L"steam://rungameid/570//-high -dx11 -fullscreen/", L"", false);
	}

	else if (game == L"smite2") {
		MessageBoxEx(nullptr, L"Select: C:\\Program Files (x86)\\Steam\\steamapps\\common\\SMITE 2", L"LoLSuite", MB_OK, 0);
		folder();
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
		url(restore ? L"restore/smite2/tbbmalloc.dll" : L"patch/tbbmalloc.dll", 7);

		Run(L"steam://rungameid/2437170", L"", false);
	}
	else if (game == L"minecraft")
	{
		PowerShell({
			L"Get-AppxPackage -Name Microsoft.DesktopAppInstaller | Foreach { Add-AppxPackage -DisableDevelopmentMode -Register \"$($_.InstallLocation)\\AppXManifest.xml\" }",
			L"winget source update"
			});
		const std::vector<std::wstring> mcprocesses = { L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe", L"Minecraft.Windows.exe" };
		for (const auto& process : mcprocesses) ExitThread(process);
		const size_t bufferSize = MAX_PATH + 1;
		char appdataBuffer[bufferSize];
		size_t retrievedSize = 0;
		errno_t err = getenv_s(&retrievedSize, appdataBuffer, bufferSize, "APPDATA");
		std::filesystem::path configPath = appdataBuffer; configPath /= ".minecraft";
		std::filesystem::remove_all(configPath);
		configPath /= "launcher_profiles.json";
		std::vector<std::wstring> commands_minecraft;
		for (auto* v : { L"JavaRuntimeEnvironment", L"JDK.17", L"JDK.18", L"JDK.19", L"JDK.20", L"JDK.21", L"JDK.22", L"JDK.23", L"JDK.24" }) commands_minecraft.emplace_back(L"winget uninstall Oracle." + std::wstring(v) + L" --purge -h");
		commands_minecraft.insert(commands_minecraft.end(), { L"winget uninstall Mojang.MinecraftLauncher --purge -h", L"winget install Oracle.JDK.24 --accept-package-agreements", L"winget install Mojang.MinecraftLauncher --accept-package-agreements" });
		PowerShell(commands_minecraft);
		Run(L"C:\\Program Files (x86)\\Minecraft Launcher\\MinecraftLauncher.exe", L"", false);
		while (!std::filesystem::exists(configPath)) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
		for (const auto& process : mcprocesses) ExitThread(process);
		std::wifstream configFile(configPath);
		configFile.imbue(std::locale("en_US.UTF-8"));
		if (configFile.is_open()) {
			std::wstring configData((std::istreambuf_iterator<wchar_t>(configFile)), std::istreambuf_iterator<wchar_t>());
			configFile.close(); std::wstringstream configStream(configData);
			std::wstring updatedConfigData, line;
			while (std::getline(configStream, line)) { if (line.find(L"\"javaDir\"") == std::wstring::npos && line.find(L"\"skipJreVersionCheck\"") == std::wstring::npos) { updatedConfigData += line + L"\n"; } }
			std::vector<std::wstring> types = { L"\"type\" : \"latest-release\"", L"\"type\" : \"latest-snapshot\"" };
			std::wstring jdkpath = L"C:\\\\Program Files\\\\Java\\\\jdk-24\\\\bin\\\\javaw.exe";
			for (const auto& type : types) {
				if (size_t typePos = updatedConfigData.find(type);
					typePos != std::wstring::npos) {
					if (size_t lineStart = updatedConfigData.rfind(L'\n', typePos); lineStart != std::wstring::npos) { updatedConfigData.insert(lineStart + 1, L" \"skipJreVersionCheck\" : true,\n"); }
					size_t javaDirPos = typePos;
					for (int i = 0; i < 4 && javaDirPos != std::wstring::npos; ++i) { javaDirPos = updatedConfigData.rfind(L'\n', javaDirPos - 1); } if (javaDirPos != std::wstring::npos) { updatedConfigData.insert(javaDirPos + 1, L" \"javaDir\" : \"" + jdkpath + L"\",\n"); }
				}
			}
			std::wofstream outFile(configPath);
			outFile.imbue(std::locale("en_US.UTF-8"));
			outFile << updatedConfigData; outFile.close();
		}
		Run(L"C:\\Program Files (x86)\\Minecraft Launcher\\MinecraftLauncher.exe", L"", false);
	}
}

void manageTasks(const std::wstring& task)
{
	if (task == L"cafe")
	{
		net(L"W32Time", false, true);

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

		WCHAR localAppData[MAX_PATH];
		if (SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
			std::filesystem::path explorerPath = std::filesystem::path(localAppData) / L"Microsoft\\Windows\\Explorer";
			for (const auto& pattern : { L"thumbcache_*.db", L"iconcache_*.db", L"ExplorerStartupLog*.etl" }) {
				WIN32_FIND_DATA data;
				std::wstring searchPath = (explorerPath / pattern).wstring();
				HANDLE hFind = FindFirstFile(searchPath.c_str(), &data);
				if (hFind != INVALID_HANDLE_VALUE) {
					do {
						std::filesystem::remove(explorerPath / data.cFileName);
					} while (FindNextFile(hFind, &data));
					FindClose(hFind);
				}
			}
		}

		std::vector<std::wstring> services = { L"wuauserv", L"BITS", L"CryptSvc" };
		for (const auto& s : services) net(s, false);

		WCHAR winDir[MAX_PATH];
		if (GetWindowsDirectory(winDir, MAX_PATH)) {
			std::filesystem::path distCache = std::filesystem::path(winDir) / L"SoftwareDistribution";
			if (std::filesystem::exists(distCache)) {
				std::filesystem::remove_all(distCache);
			}
		}

		for (const auto& s : services) net(s, true);

		for (const auto& proc : {
			L"cmd.exe",
			L"pwsh.exe",
			L"powershell.exe",
			L"WindowsTerminal.exe",
			L"OpenConsole.exe",
			L"wt.exe",
			L"Battle.net.exe",
			L"steam.exe",
			L"Origin.exe",
			L"EADesktop.exe",
			L"EpicGamesLauncher.exe"
			}) ExitThread(proc);

	std::vector<std::wstring> apps = { L"Microsoft.DirectX",
		L"Microsoft.VCRedist.2005.x64", L"Microsoft.VCRedist.2005.x86",
		L"Microsoft.VCRedist.2008.x64", L"Microsoft.VCRedist.2008.x86",
		L"Microsoft.VCRedist.2010.x64", L"Microsoft.VCRedist.2010.x86",
		L"Microsoft.VCRedist.2012.x64", L"Microsoft.VCRedist.2012.x86",
		L"Microsoft.VCRedist.2013.x64", L"Microsoft.VCRedist.2013.x86",
		L"Microsoft.VCRedist.2015+.x64", L"Microsoft.PowerShell",
		L"Microsoft.WindowsTerminal", L"9MZPRTH5C0TB", L"9MZ1SNWT0N5D",
		L"9P95ZZKTNRN4", L"9N0DX20HK701", L"9N8G5RFZ9XK3", L"9MVZQVXJBQ9V",
		L"9N4D0MSMP0PT", L"9N5TDP8VCMHS", L"9N95Q1ZZPMH4", L"9NCTDW2W1BH8",
		L"9NQPSL29BFFF", L"9PB0TRCNRHFX", L"9PCSD6N03BKV", L"9PG2DK419DRG",
		L"9PMMSR1CGPWG", L"Blizzard.BattleNet", L"ElectronicArts.EADesktop",
		L"ElectronicArts.Origin", L"EpicGames.EpicGamesLauncher", L"Valve.Steam"
		};

		std::vector<std::wstring> uninstallCommands;
		std::vector<std::wstring> installCommands;

		for (const auto& app : apps) {
			uninstallCommands.push_back(L"winget uninstall " + app + L" --purge");

			if (app == L"ElectronicArts.Origin") {
				continue; // Skip install for Origin
			}

			std::wstring command = L"winget install " + app;
			if (app == L"Blizzard.BattleNet") {
				command += L" --location \"C:\\Battle.Net\"";
			}
			command += L" --accept-package-agreements --accept-source-agreements";
			installCommands.push_back(command);
		}

		PowerShell(uninstallCommands);
		PowerShell(installCommands);


	}
}

void handleCommand(int cb, bool flag) {
	static const std::unordered_map<int, std::function<void()>> commandMap = {
		{0, [flag]() { manageGame(L"leagueoflegends", flag); }},
		{1, [flag]() { manageGame(L"dota2", flag); }},
		{2, [flag]() { manageGame(L"smite2", flag); }},
		{3, [flag]() { manageGame(L"minecraft", flag); }},
		{4, []() { manageTasks(L"cafe"); }}

	};

	if (auto it = commandMap.find(cb); it != commandMap.end()) {
		it->second();
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_COMMAND: {
		UINT id = LOWORD(wParam);
		UINT code = HIWORD(wParam);

		if (code == CBN_SELCHANGE) {
			cb_index = SendMessage(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
		}

		switch (id) {
		case 1:
			handleCommand(cb_index, false);
			return 0;

		case 2:
			handleCommand(cb_index, true);
			return 0;

		case IDM_EXIT:
			DestroyWindow(hWnd);
			return 0;
		}
		break;

		if (HIWORD(wParam) == CBN_DROPDOWN) {
			InvalidateRect(combo, nullptr, TRUE); // Force redraw
		}
	}

	case WM_CTLCOLOREDIT:
	{
		HDC hdcEdit = (HDC)wParam;
		SetBkColor(hdcEdit, RGB(255, 255, 255));       // White background
		SetTextColor(hdcEdit, RGB(0, 0, 0));           // Black text
		return (INT_PTR)CreateSolidBrush(RGB(255, 255, 255));
	}

	case WM_CTLCOLORLISTBOX:
	{
		HDC hdcList = (HDC)wParam;
		SetBkColor(hdcList, RGB(32, 32, 32));          // Dark dropdown background
		SetTextColor(hdcList, RGB(200, 200, 200));     // Light text
		return (INT_PTR)CreateSolidBrush(RGB(32, 32, 32));
	}

	case WM_CTLCOLORBTN:
	{
		HDC hdcBtn = (HDC)wParam;
		SetBkColor(hdcBtn, RGB(255, 255, 255));       // White background
		SetTextColor(hdcBtn, RGB(0, 0, 0));           // Black text
		return (INT_PTR)CreateSolidBrush(RGB(255, 255, 255)); // Return white brush
	}

	case WM_DRAWITEM:
	{
		LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;

		// Set background and text colors
		FillRect(dis->hDC, &dis->rcItem, CreateSolidBrush(RGB(255, 255, 255)));
		SetTextColor(dis->hDC, RGB(0, 0, 0));
		SetBkMode(dis->hDC, TRANSPARENT);

		// Get the button text dynamically
		wchar_t text[256];
		GetWindowText(dis->hwndItem, text, sizeof(text) / sizeof(wchar_t));

		// Draw the button text
		DrawText(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}

	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

	return 0;
}


int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nShowCmd) {
	MSG msg;

	if (OpenClipboard(nullptr)) {
		EmptyClipboard();
		CloseClipboard();
	}

	LimitInstance GUID(L"{3025d31f-c76e-435c-a4b48-9d084fa9f5ea}");
	if (LimitInstance::AnotherInstanceRunning())
		return 0;

	WNDCLASSEXW wcex = { sizeof(wcex), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)) , LoadCursor(nullptr, IDC_ARROW), CreateSolidBrush(RGB(32, 32, 32)), nullptr, L"LoLSuite", nullptr };

	RegisterClassExW(&wcex);

	// Main Window
	HWND hWnd = CreateWindowEx(
		0,
		L"LoLSuite",
		L"LoLSuite",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 330, 100, nullptr, nullptr, hInstance, nullptr
	);

	// Title Bar set dark mode
	BOOL value = true;
	DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

	// First control: "Patch" button
	hwndPatch = CreateWindowEx(
		WS_EX_TOOLWINDOW,
		L"BUTTON",
		L"Patch",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
		10, 20, 60, 30,
		hWnd,
		HMENU(1),
		hInstance,
		nullptr
	);

	// Second control: "Restore" button
	hwndRestore = CreateWindowEx(
		0,
		L"BUTTON",
		L"Restore",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		75, 20, 60, 30,
		hWnd,
		HMENU(2),
		hInstance,
		nullptr
	);

	// Third control : "Combobox"
	combo = CreateWindowEx(
		0, L"COMBOBOX", nullptr,
		CBS_DROPDOWN | WS_CHILD | WS_VISIBLE,
		150, 20, 150, 300,
		hWnd, nullptr, hInstance, nullptr
	);


	for (const auto& item : {
		L"League of Legends",
		L"Dota 2",
		L"SMITE 2",
		L"Minecraft",
		L"Game Clients"
		}) {
		SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
	}

	SendMessage(combo, CB_SETCURSEL, 0, 0);

	typedef BOOL(WINAPI* DnsFlushResolverCacheFuncPtr)();
	if (HMODULE dnsapi = LoadLibrary(L"dnsapi.dll")) {
		auto DnsFlush = reinterpret_cast<DnsFlushResolverCacheFuncPtr>(
			GetProcAddress(dnsapi, "DnsFlushResolverCache"));
		if (DnsFlush) DnsFlush();
		FreeLibrary(dnsapi);
	}

	for (const auto& proc : {
		L"firefox.exe", L"msedge.exe", L"chrome.exe", L"iexplore.exe"
		}) ExitThread(proc);

	ShellExecute(nullptr, L"open", L"RunDll32.exe", L"InetCpl.cpl, ClearMyTracksByProcess 4351", nullptr, SW_HIDE);

	wchar_t localAppData[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
		std::filesystem::path edgeCache = std::filesystem::path(localAppData) / "Microsoft" / "Edge" / "User Data" / "Default" / "Cache";
		std::filesystem::path chromeCache = std::filesystem::path(localAppData) / "Google" / "Chrome" / "User Data" / "Default" / "Cache";

		if (std::filesystem::exists(edgeCache)) std::filesystem::remove_all(edgeCache);
		if (std::filesystem::exists(chromeCache)) std::filesystem::remove_all(chromeCache);
	}

	wchar_t roamingAppData[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, roamingAppData))) {
		std::filesystem::path profilesDir = std::filesystem::path(roamingAppData) / "Mozilla" / "Firefox" / "Profiles";
		if (std::filesystem::exists(profilesDir)) {
			for (const auto& entry : std::filesystem::directory_iterator(profilesDir)) {
				if (std::filesystem::is_directory(entry)) {
					std::filesystem::path cachePath = entry.path() / "cache2";
					if (std::filesystem::exists(cachePath)) {
						std::filesystem::remove_all(cachePath);
					}
				}
			}
		}
	}

	const wchar_t* regPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VolumeCaches";
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
		wchar_t subKeyName[256];
		DWORD subKeyLen;
		DWORD enable = 1;

		for (DWORD i = 0; ; ++i) {
			subKeyLen = 256;
			if (RegEnumKeyExW(hKey, i, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
				break;

			std::wstring fullPath = std::wstring(regPath) + L"\\" + subKeyName + L"\\StateFlags001";
			HKEY hSubKey;

			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, fullPath.c_str(), 0, KEY_ALL_ACCESS, &hSubKey) == ERROR_SUCCESS) {
				RegDeleteValue(hSubKey, nullptr);
				RegCloseKey(hSubKey);
			}

			if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, fullPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hSubKey, nullptr) == ERROR_SUCCESS) {
				RegSetValueExW(hSubKey, nullptr, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&enable), sizeof(enable));
				RegCloseKey(hSubKey);
			}
		}
		RegCloseKey(hKey);
	}


	PowerShell({L"cleanmgr.exe /sagerun:1"});
	SHEmptyRecycleBin(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);

	ShowWindow(hWnd, nShowCmd);
	UpdateWindow(hWnd);
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return static_cast<int>(msg.wParam);
}