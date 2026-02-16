#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <urlmon.h>
#include <ShObjIdl_core.h>
#include <Shlobj_core.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <wininet.h>
#include <vector>
#include <fstream>
#include <thread>
#include "resource.h"

int cb_index = 0;
std::vector<std::wstring> b(159);
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

static void DPath(const std::wstring& url, int idx) {
	static const std::wstring base = L"https://lolsuite.org/";
	const std::wstring& filePath = b[idx];
	const std::wstring fullUrl = base + url;
	DeleteUrlCacheEntry(fullUrl.c_str());
	URLDownloadToFile(nullptr, fullUrl.c_str(), filePath.c_str(), 0, nullptr);

	const std::wstring zone = filePath + L":Zone.Identifier";
	if (std::filesystem::exists(zone)) {
		std::error_code ec;
		std::filesystem::remove(zone, ec);
	}
}

static void ProcKill(const std::wstring& name) {
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

static bool x64() {
	BOOL wow = FALSE;

	auto k32 = GetModuleHandleW(L"kernel32");
	if (!k32) return false;

	using Fn = BOOL(WINAPI*)(HANDLE, PBOOL);
	auto fn = reinterpret_cast<Fn>(GetProcAddress(k32, "IsWow64Process"));
	if (!fn) return false;

	return fn(GetCurrentProcess(), &wow) && wow;
}


static void ExecuteAndWait(SHELLEXECUTEINFO& sei, bool wait = true) {
	if (!ShellExecuteEx(&sei)) {
		DWORD err = GetLastError();
		return;
	}

	if (wait && sei.hProcess) {
		SetPriorityClass(sei.hProcess, HIGH_PRIORITY_CLASS);

		while (WaitForSingleObject(sei.hProcess, 50) == WAIT_TIMEOUT) {
			MSG msg;
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		CloseHandle(sei.hProcess);
	}
}

static void PowerShell(const std::vector<std::wstring>& commands) {
	std::wstring script;
	for (const auto& cmd : commands)
		script += cmd + L"; ";

	std::wstring args = L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"& { " + script + L" }\"";

	wchar_t pwshPath[MAX_PATH+1];
	bool hasPwsh = SearchPath(nullptr, L"pwsh.exe", nullptr, MAX_PATH+1, pwshPath, nullptr) != 0;

	const wchar_t* shellToUse = hasPwsh ? L"pwsh.exe" : L"powershell.exe";

	SHELLEXECUTEINFO sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
	sei.lpVerb = L"runas";
	sei.lpFile = shellToUse;
	sei.lpParameters = args.c_str();
	sei.nShow = SW_HIDE;

	ExecuteAndWait(sei);
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
	std::wstring iniPath = (std::filesystem::current_path() / L"MOBASuite.cfg").wstring();
	wchar_t savedPath[MAX_PATH] = {};
	GetPrivateProfileString(pathLabel.c_str(), L"path", L"", savedPath, MAX_PATH, iniPath.c_str());

	if (wcslen(savedPath) > 0) {
		b[0] = savedPath;
		return b[0];
	}

	std::wstring message = L"Select: " + pathLabel;
	MessageBoxEx(nullptr, message.c_str(), L"MOBASuite", MB_OK, 0);
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

struct ServiceHandleDeleter {
	void operator()(SC_HANDLE h) const {
		if (h) CloseServiceHandle(h);
	}
};


static void serviceman(const std::wstring& serviceName, bool start, bool restart = false) {
	using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ServiceHandleDeleter>;

	ServiceHandle scm(OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS));

	ServiceHandle svc(OpenService(scm.get(), serviceName.c_str(),
		SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS));

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

void ProcessGame(const GameConfig& config, bool restore) {
	browse(config.baseDir);
	for (const auto& proc : config.processes)
		ProcKill(proc);

	for (const auto& [dst, src, path] : config.cpaths)
		CPath(dst, src, path);

	for (const auto& op : config.fileOps) {
		const std::wstring& filePath = restore ? op.restorePath : op.patchPath;
		DPath(filePath, op.dstId);
	}

	Run(config.steamUrl, L"", false);
}

static void manageGame(const std::wstring& game, bool restore) {

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
		ProcessGame(dota2, restore);
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
		ProcessGame(smite2, restore);
	}
	else if (game == L"mgsΔ") {
		GameConfig mgsΔ{
			L"mgsΔ",
			L"METAL GEAR SOLID Delta Base Dir",
			{ L"MGSDelta.exe", L"MGSDelta-Win64-Shipping.exe", L"Nightmare-Win64-Shipping.exe" },
			{
				{8, 0, L"MGSDelta\\Binaries\\Win64"},
				{7, 0, L"MGSDelta_Nightmare\\Binaries\\Win64"},
				{1, 8, L"tbb.dll"},
				{2, 8, L"tbb12.dll"},
				{3, 8, L"tbbmalloc.dll"},
				{4, 7, L"tbb.dll"},
				{5, 7, L"tbb12.dll"},
				{6, 7, L"tbbmalloc.dll"}
			},
			{
				{1, 8, L"tbb.dll", L"patch/tbb.dll", L"restore/mgs/tbb.dll"},
				{2, 8, L"tbb12.dll", L"patch/tbb.dll", L"restore/mgs/tbb12.dll"},
				{3, 8, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/mgs/tbbmalloc.dll"},
				{4, 7, L"tbb.dll", L"patch/tbb.dll", L"restore/mgs/tbb.dll"},
				{5, 7, L"tbb12.dll", L"patch/tbb.dll", L"restore/mgs/tbb12.dll"},
				{6, 7, L"tbbmalloc.dll", L"patch/tbbmalloc.dll", L"restore/mgs/tbbmalloc.dll"}
			},
			L"steam://rungameid/2417610"
		};
		ProcessGame(mgsΔ, restore);
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
		ProcessGame(blands4, restore);
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
		ProcessGame(oblivionr, restore);
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
		ProcessGame(silenthillf, restore);
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
		ProcessGame(outworlds2, restore);
		}
	else if (game == L"minecraft")
	{
		for (const auto& proc : {
			L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe", L"Minecraft.Windows.exe"
			}) ProcKill(proc);
		char appdata[MAX_PATH + 1];
		size_t size = 0;
		getenv_s(&size, appdata, MAX_PATH + 1, "APPDATA");
		std::filesystem::path configPath = std::filesystem::path(appdata) / ".minecraft";
		std::filesystem::remove_all(configPath);
		configPath /= "launcher_profiles.json";
		std::vector<std::wstring> cmds;

		cmds.push_back(L"winget uninstall Mojang.MinecraftLauncher --purge");

		for (auto* v : {L"JavaRuntimeEnvironment", L"JDK.17", L"JDK.18", L"JDK.19", L"JDK.20", L"JDK.21", L"JDK.22", L"JDK.23", L"JDK.24", L"JDK.25"})
		{
			cmds.push_back(L"winget uninstall Oracle." + std::wstring(v) + L" --purge");
		}

		cmds.push_back(L"winget install Oracle.JDK.25 --accept-package-agreements");
		cmds.push_back(L"winget install Mojang.MinecraftLauncher");

		PowerShell(cmds);

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
		std::wstring jdkpath = L"C:\\\\Program Files\\\\Java\\\\jdk-25.0.2\\\\bin\\\\javaw.exe";
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
		for (const auto& proc : { L"Minecraft.exe", L"MinecraftLauncher.exe", L"javaw.exe", L"MinecraftServer.exe", L"java.exe", L"Minecraft.Windows.exe" })
		{
			ProcKill(proc);
		}

		Run(L"C:\\Program Files (x86)\\Minecraft Launcher\\MinecraftLauncher.exe", L"", false);
	}
}

static void manageTask(const std::wstring& task) {
	if (task == L"cafe") {
		for (const auto& proc : {L"cmd.exe", L"DXSETUP.exe", L"pwsh.exe", L"powershell.exe", L"WindowsTerminal.exe", L"OpenConsole.exe", L"wt.exe", L"Battle.net.exe", L"steam.exe", L"Origin.exe", L"EADesktop.exe", L"EpicGamesLauncher.exe"}) ProcKill(proc);
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
				CPath(baseIndex + i, tmpIndex, files[i]);
				DPath(L"DXSETUP/" + files[i], baseIndex + i);
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

		serviceman(L"W32Time", false, true);

		PowerShell({
			L"wsreset -i",
			L"w32tm /resync",
			L"netsh int ip reset",
			L"netsh winsock reset",
			L"netsh winhttp reset proxy",
			L"sc config tzautoupdate start= auto",
			L"powercfg -restoredefaultschemes",
			L"Add-WindowsCapability -Online -Name NetFx3~~~~",
			L"Update-MpSignature -UpdateSource MicrosoftUpdateServer",
			L"Get-AppxPackage -Name Microsoft.DesktopAppInstaller | Foreach { Add-AppxPackage -DisableDevelopmentMode -Register \"$($_.InstallLocation)\\AppXManifest.xml\" }",
			L"winget source update"
			});

		serviceman(L"tzautoupdate", true);

		std::vector<std::wstring> services = { L"wuauserv", L"BITS", L"CryptSvc" };
		for (auto& s : services) serviceman(s, false);

		WCHAR winDir[MAX_PATH + 1];
		if (GetWindowsDirectory(winDir, MAX_PATH + 1) > 0) {
			std::filesystem::remove_all(std::filesystem::path(winDir) / L"SoftwareDistribution");
		}

		for (auto& s : services)
			serviceman(s, true);

		std::vector<std::wstring> apps = {
			L"Microsoft.VCRedist.2005.x86", L"Microsoft.VCRedist.2005.x64", L"Microsoft.VCRedist.2008.x64", L"Microsoft.VCRedist.2008.x86", L"Microsoft.VCRedist.2010.x64", L"Microsoft.VCRedist.2010.x86", L"Microsoft.VCRedist.2012.x64",
			L"Microsoft.VCRedist.2012.x86", L"Microsoft.VCRedist.2013.x64", L"Microsoft.VCRedist.2013.x86", L"Microsoft.VCRedist.2015+.x64", L"Microsoft.VCRedist.2015+.x86", L"9MZ1SNWT0N5D", L"9N0DX20HK701",
			L"9MZPRTH5C0TB", L"9MZ1SNWT0N5D", L"9N4D0MSMP0PT", L"9N5TDP8VCMHS", L"9N95Q1ZZPMH4", L"9NCTDW2W1BH8", L"9NQPSL29BFFF", L"9PB0TRCNRHFX", L"9PCSD6N03BKV", L"9PG2DK419DRG", L"9PMMSR1CGPWG", L"Blizzard.BattleNet", L"ElectronicArts.EADesktop",
			L"ElectronicArts.Origin", L"EpicGames.EpicGamesLauncher", L"Valve.Steam"
		};

		std::vector<std::wstring> filteredApps;
		bool is64Bit = x64();

		for (const auto& app : apps) {
			if (!is64Bit &&
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

		PowerShell(uninstall);
		PowerShell(install);
	}

	else if (task == L"caches")
	{
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

		auto flushDnsCache = []() {
			if (HMODULE dnsapi = LoadLibrary(L"dnsapi.dll")) {
				using DnsFlushResolverCacheFuncPtr = BOOL(WINAPI*)();
				if (auto DnsFlush = reinterpret_cast<DnsFlushResolverCacheFuncPtr>(
					GetProcAddress(dnsapi, "DnsFlushResolverCache"))) {
					DnsFlush();
				}
				FreeLibrary(dnsapi);
			}
			};
		flushDnsCache();

		for (const auto& proc : { L"firefox.exe", L"msedge.exe", L"chrome.exe", L"iexplore.exe" }) {
			ProcKill(proc);
		}

		ShellExecute(nullptr, L"open", L"RunDll32.exe", L"InetCpl.cpl, ClearMyTracksByProcess 4351", nullptr, SW_HIDE);

		auto clearCacheDir = [](const std::filesystem::path& path) {
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
				std::filesystem::path base = *local;
				clearCacheDir(base / "Microsoft/Edge/User Data/Default/Cache");
				clearCacheDir(base / "Google/Chrome/User Data/Default/Cache");
			}

			if (auto roaming = getFolder(CSIDL_APPDATA)) {
				std::filesystem::path profiles = *roaming / "Mozilla/Firefox/Profiles";

				if (std::filesystem::exists(profiles)) {
					for (auto& entry : std::filesystem::directory_iterator(profiles)) {
						if (entry.is_directory()) {
							clearCacheDir(entry.path() / "cache2");
						}
					}
				}
		}


		SHEmptyRecycleBin(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
	}
}

static void handleCommand(int cbi, bool restore) {
	switch (cbi) {
	case 0: manageGame(L"dota2", restore); break;
	case 1: manageGame(L"smite2", restore); break;
	case 2: manageGame(L"mgsΔ", restore); break;
	case 3: manageGame(L"blands4", restore); break;
	case 4: manageGame(L"oblivionr", restore); break;
	case 5: manageGame(L"silenthillf", restore); break;
	case 6: manageGame(L"outworlds2", restore); break;
	case 7: manageGame(L"minecraft", restore); break;
	case 8: manageTask(L"cafe"); break;
	case 9: manageTask(L"caches"); break;
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
		const UINT id = LOWORD(wParam);
		const UINT code = HIWORD(wParam);

		if (code == CBN_SELCHANGE)
			cb_index = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);

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
		HDC hdc = (HDC)wParam;
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kButtonText);
		return (INT_PTR)GetStockObject(HOLLOW_BRUSH);
	}

	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORSCROLLBAR: {
		HDC hdc = (HDC)wParam;
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kTextColor);
		return (INT_PTR)hBrush;
	}

	case WM_DRAWITEM: {
		auto* dis = (LPDRAWITEMSTRUCT)lParam;
		if (!dis || dis->CtlType != ODT_BUTTON)
			return FALSE;

		const bool selected = (dis->itemState & ODS_SELECTED);
		const COLORREF bg = selected ? RGB(0, 120, 215) : kBackground;
		const COLORREF fg = selected ? RGB(255, 255, 255) : kButtonText;

		HBRUSH brush = CreateSolidBrush(bg);
		FillRect(dis->hDC, &dis->rcItem, brush);
		DeleteObject(brush);

		HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
		HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
		HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));

		RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
			dis->rcItem.right, dis->rcItem.bottom, 10, 10);

		SelectObject(dis->hDC, oldBrush);
		SelectObject(dis->hDC, oldPen);
		DeleteObject(pen);

		SetTextColor(dis->hDC, fg);
		SetBkMode(dis->hDC, TRANSPARENT);

		wchar_t text[256];
		GetWindowText(dis->hwndItem, text, 256);
		DrawText(dis->hDC, text, -1, &dis->rcItem,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);

		return TRUE;
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
		sizeof(wc), CS_HREDRAW | CS_VREDRAW, WndProc,
		0, 0, hInstance,
		LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)),
		LoadCursor(nullptr, IDC_ARROW),
		CreateSolidBrush(RGB(32,32,32)),
		nullptr, L"MOBASuite", nullptr
	};
	RegisterClassEx(&wc);

	HFONT font = CreateFont(
		-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI"
	);

	hWnd = CreateWindowEx(
		WS_EX_LAYERED, L"MOBASuite", L"FPS Booster",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, W, H,
		nullptr, nullptr, hInstance, nullptr
	);
	SetLayeredWindowAttributes(hWnd, 0, 229, LWA_ALPHA);

	hwndPatch = CreateWindowEx(
		0, L"BUTTON", L"Patch",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		xPatch, TOP, BW, CH,
		hWnd, HMENU(1), hInstance, nullptr
	);
	SendMessage(hwndPatch, WM_SETFONT, (WPARAM)font, TRUE);

	hwndRestore = CreateWindowEx(
		0, L"BUTTON", L"Restore",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON,
		xRestore, TOP, BW, CH,
		hWnd, HMENU(2), hInstance, nullptr
	);
	SendMessage(hwndRestore, WM_SETFONT, (WPARAM)font, TRUE);

	combo = CreateWindowEx(
		0, WC_COMBOBOX, nullptr,
		CBS_DROPDOWN | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		comboLeft, comboTop, comboWidth, 210,
		hWnd, HMENU(3), hInstance, nullptr
	);
	SendMessage(combo, WM_SETFONT, (WPARAM)font, TRUE);

	for (LPCWSTR s : {
		L"DOTA 2", L"SMITE 2", L"Metal Gear Solid Δ : Snake Eater",
			L"Borderlands 4", L"The Elder Scrolls IV: Oblivion Remastered",
			L"SILENT HILL f", L"Outer Worlds 2", L"MineCraft",
			L"Café Clients", L"Clear Cache"
	}) SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)s);

	SendMessage(combo, CB_SETCURSEL, 0, 0);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return (int)msg.wParam;
}