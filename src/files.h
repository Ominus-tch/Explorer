#pragma once

#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <execution>

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_internal.h>

#include <d3d11.h>
#include <Windows.h>

#define MAX_RESULTS 1000
#define MAX_SEARCH_DEPTH 10
#define MAX_FILE_SIZE_DEPTH 22
#define DISPLAY_RESULTS_WHILE_SEARCHING true
#define GET_FOLDER_SIZE_ON_SEARCH true

namespace fs = std::filesystem;


namespace File {

	std::string fileTimeToString(FILETIME last_changed) {
		ULARGE_INTEGER uli;
		uli.LowPart = last_changed.dwLowDateTime;
		uli.HighPart = last_changed.dwHighDateTime;

		// Convert to time_t
		time_t fileTime = (uli.QuadPart - 116444736000000000) / 10000000;

		// Convert to struct tm
		struct tm tm;
		localtime_s(&tm, &fileTime);

		// Format the time into a string
		char buffer[20]; // Sufficient buffer size
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);

		return std::string(buffer);
	}

	const std::vector<std::string> largeFiles = { "Program Files", "Program Files (x86)", "Windows", "Users" };

	struct FileInfo {
		std::string name;
		uintmax_t size;
		std::string type;
		std::string path;
		FILETIME last_changed;
	};

	struct FolderInfo {
		std::string name;
		std::string path;
		FILETIME last_changed;
	};

	struct Drive {
		char chr;
		std::string name;
		ULONGLONG capacity, used_space, free_space;

		void GetUsedSpace() {
			used_space = capacity - free_space;
		}
	};

	std::unordered_map<std::string, uint64_t> FolderSizeCache;
	std::pair<std::vector<FileInfo>, std::vector<FolderInfo>> FileCache;
	std::string prevPath = "";

	static std::string FormatFileSize(uint64_t size) {
		constexpr uint64_t KB = 1024;
		constexpr uint64_t MB = 1024 * KB;
		constexpr uint64_t GB = 1024 * MB;
		constexpr uint64_t TB = 1024 * GB;

		std::ostringstream oss;
		oss << std::fixed << std::setprecision(2);

		if (size >= TB) {
			oss << static_cast<double>(size) / TB << " TB";
		}
		else if (size >= GB) {
			oss << static_cast<double>(size) / GB << " GB";
		}
		else if (size >= MB) {
			oss << static_cast<double>(size) / MB << " MB";
		}
		else if (size >= KB) {
			oss << static_cast<double>(size) / KB << " KB";
		}
		else {
			oss << size << " B";
		}

		return oss.str();
	}
}

namespace ImGui {

	static inline ImVec2  operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y); }
	static inline ImVec2  operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y); }

	bool DriveButton(const File::Drive& drive, const ImVec2& size_arg = ImVec2(0, 0), ImGuiButtonFlags_ flags = ImGuiButtonFlags_None)
	{
		std::string drive_name = drive.name + " (" + (char)std::toupper(drive.chr) + ":)\n\n" + File::FormatFileSize(drive.free_space) + " free of " + File::FormatFileSize(drive.capacity);
		const char* label = drive_name.c_str();

		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID(label);
		ImVec2 label_size = CalcTextSize(label, NULL, true);

		//label_size.y *= 1.25f;

		ImVec2 pos = window->DC.CursorPos;
		if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
			pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
		ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

		const ImRect bb(pos, pos + size);

		ItemSize(size, style.FramePadding.y);
		if (!ItemAdd(bb, id))
			return false;

		bool hovered, held;
		bool pressed = ButtonBehavior(bb, id, &hovered, &held, flags);

		{ // Progress Bar
			float fraction = ((float)drive.used_space / (float)drive.capacity);

			float oneLineHeight = CalcTextSize("T").y;
			pos.y += oneLineHeight * 1.25f;
			ImVec2 size_arg_2 = { size.x, oneLineHeight };

			ImVec2 size = CalcItemSize(size_arg_2, CalcItemWidth(), g.FontSize + style.FramePadding.y * 2.0f);
			ImRect bb(pos, pos + size);
			ItemSize(size, style.FramePadding.y);

			// Render
			fraction = ImSaturate(fraction);
			RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
			bb.Expand(ImVec2(-style.FrameBorderSize, -style.FrameBorderSize));
			const ImVec2 fill_br = ImVec2(ImLerp(bb.Min.x, bb.Max.x, fraction), bb.Max.y);
			RenderRectFilledRangeH(window->DrawList, bb, GetColorU32(ImGuiCol_PlotHistogram), 0.0f, fraction, style.FrameRounding);

			// Default displaying the fraction as percentage string, but user can override it
			//char overlay_buf[32];
			//if (!overlay)
			//{
			//	ImFormatString(overlay_buf, IM_ARRAYSIZE(overlay_buf), "%.0f%%", fraction * 100 + 0.01f);
			//	overlay = overlay_buf;
			//}

			//ImVec2 overlay_size = CalcTextSize(overlay, NULL);
			//if (overlay_size.x > 0.0f)
			//	RenderTextClipped(ImVec2(ImClamp(fill_br.x + style.ItemSpacing.x, bb.Min.x, bb.Max.x - overlay_size.x - style.ItemInnerSpacing.x), bb.Min.y), bb.Max, overlay, NULL, &overlay_size, ImVec2(0.0f, 0.5f), &bb);
		}

		// Render
		const ImU32 col = GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		RenderNavHighlight(bb, id);
		RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

		if (g.LogEnabled)
			LogSetNextTextDecoration("[", "]");
		RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);

		// Automatically close popups
		//if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
		//    CloseCurrentPopup();

		IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
		return pressed;
	}

}

struct SearchResult {
	std::string path = "";
	FILETIME last_changed;
	std::string type = "";
	uint64_t size = 0;
	uint32_t depth = 0;
};

namespace File {

	bool showingWarningWindow = false;
	std::string warningWindowText = "";
	int searchDepthMax = MAX_SEARCH_DEPTH;
	bool displayResultsWhileSearching = DISPLAY_RESULTS_WHILE_SEARCHING;
	bool getFolderSizeOnSearch = GET_FOLDER_SIZE_ON_SEARCH;

	std::vector<std::string> results;
	std::vector<SearchResult> results2;
	std::mutex resultsMutex;

	ImGuiTableSortSpecs* resultSpecs = nullptr;

	std::atomic<bool> showingResults(false);
	std::atomic<bool> isSearching(false);
	std::atomic<bool> cancelSearch(false);

	std::atomic<int> activeThreads(0);


	std::chrono::steady_clock::time_point startSearchTime;
	std::chrono::milliseconds elapsedTime;

	ULONGLONG totalUsedDiskSpace = 0;
	std::string formattedTotalUsedDiskSpace = "";
	std::atomic<ULONGLONG> bytesRead(0); // Used for rough progress

	bool showProperties = false;
	fs::path propertiesPath = "";
	std::atomic<ULONGLONG> currentPropertiesSize(0);
	std::atomic<ULONG> currentPropertiesFileCount(0);
	std::atomic<ULONG> currentPropertiesFolderCount(0);
	bool isScanning = false;

	std::chrono::steady_clock::time_point startScanTime;
	std::chrono::milliseconds elapsedScanTime;

	Drive* currentPropertySelectedDrive;

	//static std::vector<char> drives;
	static std::vector<Drive> drives;

	ImFont* fontAwesomeFont = nullptr;

	char searchQuery[256];
	char pathQuery[256];

	static fs::path currentDirectory = "";

	fs::path lastClickedPath;
	double lastClickTime = 0.0;

	FILE* consoleFile = nullptr;

	bool settingsWindow;

	void DebugSetup()
	{
		AllocConsole();
		freopen_s(&consoleFile, "CONOUT$", "w", stdout);

		std::cout << "Debug Mode Started!\n";
	}

	void DebugEnd()
	{
		if (consoleFile)
			fclose(consoleFile);
		FreeConsole();
		consoleFile = nullptr;
	}

	std::string ConvertWCharToString(const WCHAR* wstr) {
		int length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
		if (length == 0) {
			// Error handling
			return "";
		}
		std::string str(length - 1, 0);
		WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], length, nullptr, nullptr);
		return str;
	}

	/*void OpenFile(fs::path& path)
	{
		std::string filename = path.string();
		std::string command = "notepad.exe \"" + filename + "\"";
		int result = std::system(command.c_str());

		if (result != 0)
			std::cerr << "Error: Unable to open file in Notepad" << "\n";
		else
			std::cout << "Opened file: " << path.string().c_str() << "\n";
	}*/

	bool OpenFile(const fs::path& path) {
		HINSTANCE result = ShellExecuteA(NULL, "open", path.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		if (reinterpret_cast<uintptr_t>(result) > 32) {
			std::cout << "File opened successfully." << std::endl;
			return true;
		}
		else {
			std::cerr << "Failed to open file." << std::endl;
			return false;
		}
	}

	static void GoBack(int count = 1)
	{
		auto old = currentDirectory;
		lastClickedPath = old;
		for (int i = 0; i < count; i++)
		{
			currentDirectory = currentDirectory.parent_path();
		}

		if (old == currentDirectory)
			currentDirectory = "";
	}

	std::string toLower(const std::string& str) {
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return std::tolower(c);
			});
		return result;
	}

	std::string ExtractFileType(const std::string& fileName) {
		size_t dotPos = fileName.find_last_of('.');
		if (dotPos != std::string::npos) {
			return fileName.substr(dotPos + 1); // Extract the extension
		}
		return ""; // No extension found
	}

	bool IsWCharEmpty(const WCHAR* wstr) {
		return (wstr == nullptr || wstr[0] == L'\0');
	}

	static int CompareFileTimeWrapper(const FILETIME& a, const FILETIME& b) {
		return CompareFileTime(&a, &b);
	}

	void LoadFonts()
	{
		//fs::path currentPath = fs::current_path();
		//fs::path fontPath = currentPath / "file" / "fa-solid.h";

		//ImGuiIO& io = ImGui::GetIO();

		//fontAwesomeFont = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 16.0f);
		//fontAwesomeFont = io.Fonts->AddFontFromMemoryTTF((void*)fa_solid, fa_solid_size, 16.f);
		//auto* a = io.Fonts->AddFontDefault();
		//std::cout << "Font: " << fontAwesomeFont << "\n";
	}

	void GetDriveInformation() {
		totalUsedDiskSpace = 0;
		DWORD bufferSize = GetLogicalDriveStringsW(0, nullptr);
		if (bufferSize > 0) {
			std::vector<wchar_t> buffer(bufferSize);
			if (GetLogicalDriveStringsW(bufferSize, buffer.data())) {
				wchar_t* drive = buffer.data();
				while (*drive != L'\0') {
					char drive_chr = static_cast<char>(*drive);

					std::string drivePath = std::string(1, drive_chr) + ":\\";

					WCHAR szVolumeName[MAX_PATH];
					BOOL bSucceeded = GetVolumeInformationW(L"C:\\",
						szVolumeName,
						MAX_PATH,
						NULL,
						NULL,
						NULL,
						NULL,
						0);

					std::string name;

					if (IsWCharEmpty(szVolumeName))
						name = "Local Disk";
					else
						name = ConvertWCharToString(szVolumeName);

					ULONGLONG freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
					if (GetDiskFreeSpaceExW(drive, (PULARGE_INTEGER)&freeBytesAvailable, (PULARGE_INTEGER)&totalNumberOfBytes, (PULARGE_INTEGER)&totalNumberOfFreeBytes)) {
						
						totalUsedDiskSpace += totalNumberOfBytes - totalNumberOfFreeBytes;
					}
					drive += wcslen(drive) + 1;

					Drive drive;
					drive.chr = drive_chr;
					drive.capacity = totalNumberOfBytes;
					drive.free_space = totalNumberOfFreeBytes;
					drive.used_space = totalNumberOfBytes - totalNumberOfFreeBytes;
					drive.name = name;
					drives.push_back(drive);
				}
			}
		}
	}

	std::pair<std::vector<FileInfo>, std::vector<FolderInfo>> GetFiles(const std::string& directoryPath) {
		std::vector<FileInfo> files;
		std::vector<FolderInfo> folders;

		/*if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
			return { files, folders };
		}*/

		std::string searchPath = directoryPath + "\\*";

		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {

					std::string file_path = directoryPath;
					if (!directoryPath.empty() && directoryPath.back() != '\\')
						file_path.push_back('\\');
					file_path.append(findData.cFileName);

					if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					{
						FolderInfo folderInfo;
						folderInfo.name = findData.cFileName;
						folderInfo.path = file_path;

						folderInfo.last_changed.dwHighDateTime = findData.ftLastWriteTime.dwHighDateTime;
						folderInfo.last_changed.dwLowDateTime = findData.ftLastWriteTime.dwLowDateTime;
						//FILETIME last_changed = (findData.ftLastWriteTime.dwHighDateTime << 32) | findData.ftLastWriteTime.dwLowDateTime;

						folders.push_back(folderInfo);
					}
					else
					{
						FileInfo fileInfo;
						fileInfo.name = findData.cFileName;
						fileInfo.path = file_path;

						fileInfo.last_changed.dwHighDateTime = findData.ftLastWriteTime.dwHighDateTime;
						fileInfo.last_changed.dwLowDateTime = findData.ftLastWriteTime.dwLowDateTime;

						fileInfo.type = ExtractFileType(findData.cFileName);
						fileInfo.size = (findData.nFileSizeHigh * (MAXDWORD + 1)) + findData.nFileSizeLow;
						files.push_back(fileInfo);
					}
				}
			} while (FindNextFileA(hFind, &findData)); // Use FindNextFileA for narrow characters
			FindClose(hFind);
		}

		return std::make_pair(files, folders);
	}

	std::pair<std::vector<FileInfo>, std::vector<FolderInfo>> GetFiles2(const std::string& directoryPath) {
		std::vector<FileInfo> files;
		std::vector<FolderInfo> folders;

		if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
			return { files, folders };
		}

		std::cout << "Directory iterator: " << directoryPath << "\n";

		for (const auto& entry : fs::directory_iterator(directoryPath)) {
			try
			{
				auto& entry_path = entry.path();
				auto entry_path_filename = entry_path.filename();
				std::cout << "Entry: " << entry_path_filename << "\n";
				std::string entry_path_filename_string = entry_path_filename.string();
				if (entry.is_directory()) {
					if (entry_path_filename != "." && entry_path_filename != "..") {
						FolderInfo folderInfo;
						folderInfo.name = entry_path_filename_string;
						folderInfo.path = entry_path.string();
						//folderInfo.last_changed = fs::last_write_time(entry);
						folders.push_back(folderInfo);
					}
				}
				else {
					FileInfo fileInfo;
					fileInfo.name = entry_path_filename_string;
					fileInfo.path = entry_path.string();
					//fileInfo.last_changed = fs::last_write_time(entry);
					fileInfo.type = ExtractFileType(entry_path_filename_string);
					fileInfo.size = fs::file_size(entry);
					files.push_back(fileInfo);
				}
			}
			catch (const std::exception& e)
			{
				std::cerr << e.what() << "\n";
			}

		}

		return { files, folders };
	}

	bool IsSubstringPresent(const std::string& str, const std::string& substring) {
		if (substring.size() > str.size())
			return false;

		auto low_str = toLower(str);
		auto low_sub_string = toLower(substring);
		return low_str.find(low_sub_string) != std::string::npos;
	}

	static uint64_t GetFolderSize(const std::string& path, uint32_t depth = 0)
	{
		auto files = GetFiles(path);
		uint64_t foldersSize = 0;

		depth++;

		currentPropertiesFileCount += (uint64_t)files.first.size();
		for (const auto& file : files.first)
		{
			//currentPropertiesSize += fs::file_size(file.path);
			foldersSize += file.size;
		}
		currentPropertiesSize += foldersSize;

		currentPropertiesFolderCount += (uint64_t)files.second.size();

		if (depth < 10)
		{
			std::for_each(std::execution::par, files.second.begin(), files.second.end(),
				[&foldersSize, depth](const FolderInfo& folder)
				{
					foldersSize += GetFolderSize(folder.path, depth);
				});
		}
		else
		{
			for (const auto& folder : files.second)
			{
				foldersSize += GetFolderSize(folder.path, depth);
			}
		}

		resultsMutex.lock();
		FolderSizeCache[path] = foldersSize;
		resultsMutex.unlock();

		elapsedScanTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startScanTime);

		return foldersSize;

		//std::cout << "Path: " << path << " Size: " << FormatFileSize(FolderSizeCache[path]) << "\n";
	}

	static void SearchFiles(const std::string& path, const std::string& query, uint32_t depth = 0, bool seperateThread = false)
	{
		if (cancelSearch) return;
		if (path.empty()) return;
		if (++depth > searchDepthMax) return;

		if (seperateThread)
		{
			std::cout << "Created separate thread for " << path << "\n";
			activeThreads++;
		}

		auto files = GetFiles(path);
		//auto files2 = GetFiles2(path);
		for (const auto& file : files.first)
		{
			if (toLower(file.name).find(query) != std::string::npos)
			{
				resultsMutex.lock();
				//results.push_back(file.path);
				results2.push_back({ file.path, file.last_changed, file.type, file.size, depth });
				resultsMutex.unlock();
			}
			bytesRead += file.size;
		}

		if (depth < 10)
		{
			std::for_each(std::execution::par, files.second.begin(), files.second.end(),
				[&query, depth](const FolderInfo& folder)
				{
					if (toLower(folder.name).find(query) != std::string::npos)
					{
						uint64_t folderSize = 0;

						auto it = FolderSizeCache.find(folder.path);
						if (it != FolderSizeCache.end())
							folderSize = it->second;
						else if (getFolderSizeOnSearch)
							folderSize = GetFolderSize(folder.path);

						resultsMutex.lock();
						//results.push_back(folder.path);
						results2.push_back({ folder.path, folder.last_changed, "", folderSize, depth});
						resultsMutex.unlock();
					}

					SearchFiles(folder.path, query, depth);
				});
		}
		else
		{
			for (const auto& folder : files.second)
			{
				if (toLower(folder.name).find(query) != std::string::npos)
				{
					uint64_t folderSize = 0;

					resultsMutex.lock();
					auto it = FolderSizeCache.find(folder.path);
					if (it != FolderSizeCache.end())
						folderSize = it->second;
					else if (getFolderSizeOnSearch)
						folderSize = GetFolderSize(folder.path);

					//results.push_back(folder.path);
					results2.push_back({ folder.path, folder.last_changed, "", folderSize, depth });
					resultsMutex.unlock();
				}

				SearchFiles(folder.path, query, depth);
			}
		}

		if (seperateThread)
			activeThreads--;
	}

	static void StartGetFileSize(const std::string& path)
	{
		currentPropertiesSize = 0;
		currentPropertiesFolderCount = 0;
		currentPropertiesFileCount = 0;

		std::thread scanThread(GetFolderSize, path, 0);
		scanThread.detach();
		//currentPropertiesSize += GetFileSize(path);
	}

	static void GetFoldersSizes(std::vector<FolderInfo> folders)
	{
		isScanning = true;

		for (const auto& folder : folders)
		{
			bool isLargeFile = false;

			for (const std::string& element : largeFiles) {
				if (element == folder.name) {
					std::thread scanThread(GetFolderSize, folder.path, 0);
					scanThread.detach();
					isLargeFile = true;
					break;
				}
			}

			if (isLargeFile)
				continue;


			GetFolderSize(folder.path);

			//elapsedScanTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startScanTime);
		}

		isScanning = false;
	}

	static void StartStorageScan(const Drive& drive)
	{
		startScanTime = std::chrono::steady_clock::now();
		currentPropertiesSize = 0;
		currentPropertiesFolderCount = 0;
		currentPropertiesFileCount = 0;

		//std::string path = (char)std::toupper(drive.chr) + ":";
		std::string path = std::string(1, std::toupper(drive.chr)) + ":";
		auto files = GetFiles(path);

		std::thread scanThread(GetFoldersSizes, files.second);
		scanThread.detach();
	}

	static void StartFullStorageScan()
	{
		startScanTime = std::chrono::steady_clock::now();
		currentPropertiesSize = 0;
		currentPropertiesFolderCount = 0;
		currentPropertiesFileCount = 0;

		for (const auto& drive : drives)
		{
			std::string path = std::string(1, std::toupper(drive.chr)) + ":";
			auto files = GetFiles(path);

			std::thread scanThread(GetFoldersSizes, files.second);
			scanThread.detach();
		}
	}

	static bool SearchDrive(char drive, const std::string& query)
	{
		if (cancelSearch) { activeThreads--;  return true; }

		char drive_str = static_cast<char>(std::toupper(drive));
		std::string path = std::string(1, std::toupper(drive)) + ":";
		std::cout << "Scanning drive " << path << "\n";

		auto files = GetFiles(path);
		//auto files2 = GetFiles2(path);
		resultsMutex.lock();
		for (const auto& file : files.first)
		{
			if (toLower(file.name).find(query) != std::string::npos)
			{
				//results.push_back(file.path);
				results2.push_back({ file.path, file.last_changed, file.type, file.size });
			}
			bytesRead += file.size;
		}
		resultsMutex.unlock();

		for (const auto& folder : files.second)
		{
			if (toLower(folder.name).find(query) != std::string::npos)
			{
				resultsMutex.lock();
				//results.push_back(folder.path);
				results2.push_back({ folder.path, folder.last_changed });
				resultsMutex.unlock();
			}

			bool isLargeFile = false;

			for (const std::string& element : largeFiles) {
				if (element == folder.name) {
					std::thread searchThread(SearchFiles, folder.path, query, 1, true);
					searchThread.detach();
					isLargeFile = true;
					break;
				}
			}

			if (isLargeFile)
				continue;

			SearchFiles(folder.path, query, 1);
		}

		activeThreads--;
		return true;
	}

	bool Search(std::string query) {
		std::cout << "Starting search...\n";

		results.clear();
		results2.clear();
		bytesRead = 0;

		activeThreads = (int)drives.size();

		isSearching = true;
		showingResults = true;

		query = toLower(query);

		for (auto drive : drives)
		{
			std::thread searchThread(SearchDrive, drive.chr, query);
			searchThread.detach();
		}

		return true;
	}

	static void CenteredText(const char* text) {
		// Get the window width
		ImVec2 windowSize = ImGui::GetWindowSize();
		// Get the text size
		ImVec2 textSize = ImGui::CalcTextSize(text);
		// Calculate the position to start the text so that it is centered
		float textPosX = (windowSize.x - textSize.x) / 2.0f;
		// Ensure the text position is within the window bounds
		if (textPosX > 0.0f) {
			ImGui::SetCursorPosX(textPosX);
		}
		// Render the text
		ImGui::Text("%s", text);
	}

	static void ShowPropertiesWindow(const fs::path& path) {
		if (showProperties) {
			ImGui::Begin("Properties", &showProperties); // Window title is "Properties"
			ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
			//ImGui::SetWindowFocus();

			if (!path.empty())
			{

				if (fs::is_regular_file(path)) {
					ImGui::Text("Path: %s", path.string().c_str());
					ImGui::Text("Type: File");
					ImGui::Text("Size: %s", FormatFileSize(fs::file_size(path)).c_str());
				}
				else if (fs::is_directory(path)) {
					ImGui::Text("Path: %s", path.string().c_str());
					ImGui::Text("Type: File Folder");
				}
				else {
					const Drive& drive = *currentPropertySelectedDrive;
					ImGui::Text("Type: %s", drive.name.c_str());
					ImGui::Text("Used Space: %s", FormatFileSize(drive.used_space).c_str());
					ImGui::Text("Free Space: %s", FormatFileSize(drive.free_space).c_str());
					ImGui::Text("Capacity: %s", FormatFileSize(drive.capacity).c_str());

					ImGui::Text("Drive %c:", (char)std::toupper(drive.chr));
					//ImGui::SameLine();

					//if (ImGui::Button("Details"))
					//{
					//	//StartFullStorageScanOnDrive(drive);
					//}

					if (ImGui::Button("Storage Scan"))
					{
						StartStorageScan(drive);
					}

				}
			}

			if (ImGui::Button("Full Storage Scan")) 
			{
				StartFullStorageScan();
			}

			std::stringstream ss; ss << std::fixed << std::setprecision(3) << (float)elapsedScanTime.count() / 1000.f;
			std::string progress_str = "Elapsed Time: " + ss.str() + "s ";

			float prc = ((float)currentPropertiesSize / (float)totalUsedDiskSpace) * 100.f;
			std::stringstream prc_ss; prc_ss << std::fixed << std::setprecision(2) << prc;
			ImGui::Text("%s %s", progress_str.c_str(), ("(" + prc_ss.str() + "%)").c_str());

			ImGui::Text("Size: %s", FormatFileSize(currentPropertiesSize).c_str());
			ImGui::Text("Files: %lld", currentPropertiesFileCount.load());
			ImGui::Text("Folders: %lld", currentPropertiesFolderCount.load());

			ImGui::End();
		}
	}

	static void DrawResults()
	{
		if (isSearching && !displayResultsWhileSearching)
			return;

		if (ImGui::BeginTable("files", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable))
		{
			ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 0.0f, 0);
			ImGui::TableSetupColumn("Date Modified", ImGuiTableColumnFlags_None, 0.0f, 1);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 0.0f, 2);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 0.0f, 3);
			ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_None, 0.1f, 4);

			ImGui::TableHeadersRow();

			//ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
			resultSpecs = ImGui::TableGetSortSpecs();
			if (resultSpecs && resultSpecs->SpecsDirty)
			{
				if (resultSpecs->Specs->ColumnIndex == 0) // path
				{
					if (resultSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return toLower(a.path) < toLower(b.path);
							});
					}
					else
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return toLower(a.path) > toLower(b.path);
							});
					}
				}

				if (resultSpecs->Specs->ColumnIndex == 1) // last changed
				{
					if (resultSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return CompareFileTimeWrapper(a.last_changed, b.last_changed) < 0;
							});
					}
					else {
						// Sort files by file_changed in descending order
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return CompareFileTimeWrapper(a.last_changed, b.last_changed) > 0;
							});
					}
				}

				if (resultSpecs->Specs->ColumnIndex == 2) // type
				{
					if (resultSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return toLower(a.type) < toLower(b.type);
							});
					}
					else
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return toLower(a.type) > toLower(b.type);
							});
					}
				}

				if (resultSpecs->Specs->ColumnIndex == 3) // size
				{
					if (resultSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return a.size < b.size;
							});
					}
					else
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return a.size > b.size;
							});
					}
				}

				if (resultSpecs->Specs->ColumnIndex == 4) // depth
				{
					if (resultSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return a.depth < b.depth;
							});
					}
					else
					{
						std::sort(results2.begin(), results2.end(), [](const auto& a, const auto& b) {
							return a.depth > b.depth;
							});
					}
				}

				resultSpecs->SpecsDirty = false; // Mark specs as not dirty
			}

			// Draw Results
			resultsMutex.lock();
			for (const auto& result : results2)
			{
				bool* selected = new bool(false);

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);

				if (lastClickedPath == result.path)
				{
					ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.06f, 0.39f, 0.78f, 1.00f));
					*selected = true;
				}

				if (ImGui::Selectable(result.path.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					double currentTime = ImGui::GetTime();
					if (currentTime - lastClickTime < 0.5) { // Check if it's a double-click
						if (result.path == lastClickedPath)
						{
							currentDirectory = result.path;
							lastClickedPath = result.path;
							lastClickTime = currentTime;

							if (!fs::is_directory(result.path))
								GoBack();

							strcpy_s(pathQuery, sizeof(pathQuery), result.path.c_str());
							showingResults = false;
							cancelSearch = true;

							ImGui::PopStyleColor();

							break;
						}
					}
					lastClickedPath = result.path;
					lastClickTime = currentTime;
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", fileTimeToString(result.last_changed).c_str());

				ImGui::TableSetColumnIndex(2);
				if (!result.type.empty())
					ImGui::Text("%s", result.type.c_str());
				else
					ImGui::Text("%s", "File Folder");

				ImGui::TableSetColumnIndex(3);
				if (result.size != 0)
					ImGui::Text("%s", FormatFileSize(result.size).c_str());

				ImGui::TableSetColumnIndex(4);
				ImGui::Text(" %i", result.depth);
			}
			resultsMutex.unlock();

			ImGui::EndTable();
		}

		///// OLD /////


		//resultsMutex.lock();
		//for (const auto& result : results)
		//{
		//	if (lastClickedPath == result)
		//	{
		//		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.39f, 0.78f, 1.00f));
		//	}

		//	if (ImGui::Button(result.c_str()))
		//	{
		//		double currentTime = ImGui::GetTime();
		//		if (currentTime - lastClickTime < 0.5) { // Check if it's a double-click
		//			if (result == lastClickedPath)
		//			{
		//				lastClickedPath = result;
		//				currentDirectory = result;

		//				if (!fs::is_directory(result))
		//					GoBack();

		//				strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
		//				showingResults = false;
		//				cancelSearch = true;

		//				ImGui::PopStyleColor();

		//				break;
		//			}
		//		}
		//		lastClickedPath = result;
		//		lastClickTime = currentTime;
		//	}

		//	if (lastClickedPath == result)
		//	{
		//		ImGui::PopStyleColor();
		//	}
		//}
		//resultsMutex.unlock();
	}

	static void DrawFiles(const fs::path& path)
	{
		if (path.empty()) {
			for (auto& drive : drives) {
				std::string path_string = std::string(1, std::toupper(drive.chr)) + ":";
				const char* path_str = path_string.c_str();

				if (lastClickedPath == path_str)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.39f, 0.78f, 1.00f));
				}

				//std::cout << path << "\n";
				//ImGui::PushFont(fontAwesomeFont);
				std::string drive_name = drive.name + " (" + path_string + ")";

				//const char folder_icon_utf8[] = { 0xF0, 0x9F, 0x93, 0x81, '\0' };
				//std::string folder_icon_string(folder_icon_utf8);

				// Display the UTF-8 encoded string using ImGui
				//ImGui::Text("%s", folder_icon_string.c_str());
				if (ImGui::DriveButton(drive))
				//if (ImGui::Button(drive_name.c_str()))
				{
					double currentTime = ImGui::GetTime();
					if (currentTime - lastClickTime < 0.5) { // Check if it's a double-click
						if (path_str == lastClickedPath)
						{
							currentDirectory = (std::string(path_str) + "\\");
							strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
						}
					}

					lastClickedPath = path_str;
					lastClickTime = currentTime;
				}

				if (ImGui::BeginPopupContextItem(path_str)) {
					if (ImGui::MenuItem("Open")) {
						// Handle the open action
						currentDirectory = path_str;
						strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
					}
					if (ImGui::MenuItem("Properties")) {
						//propertiesPath = path_str;
						propertiesPath = std::to_string(drive.chr);
						showProperties = true;
						currentPropertySelectedDrive = &drive;
					}
					ImGui::EndPopup();
				}
				//ImGui::PopFont();

				if (lastClickedPath == path_str)
				{
					ImGui::PopStyleColor();
				}
			}


			return;
		}

		if (ImGui::BeginTable("files", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
			ImGui::TableSetupColumn("Date Modified", ImGuiTableColumnFlags_None, 0.0f, 1);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 0.0f, 2);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 0.0f, 3);

			ImGui::TableHeadersRow();


			std::string path_str = path.string();
			if (prevPath != path_str)
			{
				FileCache = GetFiles(path_str);
				prevPath = path_str;
			}

			ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
			if (sortSpecs && sortSpecs->SpecsDirty)
			{
				if (sortSpecs->Specs->ColumnIndex == 0)
				{
					if (sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(FileCache.second.begin(), FileCache.second.end(), [](const auto& a, const auto& b) {
							return toLower(a.name) < toLower(b.name);
							});

						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const auto& a, const auto& b) {
							return toLower(a.name) < toLower(b.name);
							});
					}
					else
					{
						std::sort(FileCache.second.begin(), FileCache.second.end(), [](const auto& a, const auto& b) {
							return toLower(a.name) > toLower(b.name);
							});

						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const auto& a, const auto& b) {
							return toLower(a.name) > toLower(b.name);
							});
					}
				}

				if (sortSpecs->Specs->ColumnIndex == 1)
				{
					if (sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const FileInfo& a, const FileInfo& b) {
							return CompareFileTimeWrapper(a.last_changed, b.last_changed) < 0;
							});

						// Sort folders by file_changed in ascending order
						std::sort(FileCache.second.begin(), FileCache.second.end(), [](const FolderInfo& a, const FolderInfo& b) {
							return CompareFileTimeWrapper(a.last_changed, b.last_changed) < 0;
							});
					}
					else {
						// Sort files by file_changed in descending order
						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const FileInfo& a, const FileInfo& b) {
							return CompareFileTimeWrapper(a.last_changed, b.last_changed) > 0;
							});

						// Sort folders by file_changed in descending order
						std::sort(FileCache.second.begin(), FileCache.second.end(), [](const FolderInfo& a, const FolderInfo& b) {
							return CompareFileTimeWrapper(a.last_changed, b.last_changed) > 0;
							});
					}
				}

				if (sortSpecs->Specs->ColumnIndex == 2)
				{
					if (sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const auto& a, const auto& b) {
							return toLower(a.type) < toLower(b.type);
							});
					}
					else
					{
						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const auto& a, const auto& b) {
							return toLower(a.type) > toLower(b.type);
							});
					}
				}

				if (sortSpecs->Specs->ColumnIndex == 3)
				{
					if (sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending)
					{
						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const auto& a, const auto& b) {
							return a.size < b.size;
							});

						resultsMutex.lock();
						std::sort(FileCache.second.begin(), FileCache.second.end(), [](const FolderInfo& a, const FolderInfo& b) {
							auto a_it = FolderSizeCache.find(a.path);
							auto b_it = FolderSizeCache.find(b.path);

							if (a_it != FolderSizeCache.end() && b_it != FolderSizeCache.end()) {
								// Both sizes are found in the cache, compare them
								return a_it->second < b_it->second;
							}
							else if (a_it != FolderSizeCache.end()) {
								// Only a is found in the cache, treat it as smaller
								return true;
							}
							else if (b_it != FolderSizeCache.end()) {
								// Only b is found in the cache, treat a as larger
								return false;
							}
							else {
								// Neither are found in the cache, compare paths lexicographically as a fallback
								return toLower(a.name) < toLower(b.name);
							}
							});
						resultsMutex.unlock();
					}
					else
					{
						std::sort(FileCache.first.begin(), FileCache.first.end(), [](const auto& a, const auto& b) {
							return a.size > b.size;
							});

						resultsMutex.lock();
						std::sort(FileCache.second.begin(), FileCache.second.end(), [](const FolderInfo& a, const FolderInfo& b) {
							auto a_it = FolderSizeCache.find(a.path);
							auto b_it = FolderSizeCache.find(b.path);

							if (a_it != FolderSizeCache.end() && b_it != FolderSizeCache.end()) {
								// Both sizes are found in the cache, compare them
								return a_it->second > b_it->second;
							}
							else if (a_it != FolderSizeCache.end()) {
								// Only a is found in the cache, treat it as larger
								return true;
							}
							else if (b_it != FolderSizeCache.end()) {
								// Only b is found in the cache, treat a as smaller
								return false;
							}
							else {
								// Neither are found in the cache, compare paths lexicographically as a fallback
								return a.path > b.path;
							}
							});
						resultsMutex.unlock();
					}
				}
				//sortSpecs->SpecsDirty = false; // Mark specs as not dirty
			}


			fs::path path = "";

			if (sortSpecs && sortSpecs->Specs->SortDirection == ImGuiSortDirection_Descending)
				goto files;

folders:
			for (const auto& folder : FileCache.second)
			{
				fs::path folder_path = folder.path;
				bool* selected = new bool(false);

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);

				if (lastClickedPath == folder_path)
				{
					ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.06f, 0.39f, 0.78f, 1.00f));
					*selected = true;
				}

				if (ImGui::Selectable(folder.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					double currentTime = ImGui::GetTime();
					if (currentTime - lastClickTime < 0.5) { // Check if it's a double-click
						if (folder_path == lastClickedPath)
						{
							currentDirectory = folder_path;
							strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
						}
					}
					lastClickedPath = folder_path;
					lastClickTime = currentTime;
				}

				if (ImGui::BeginPopupContextItem(folder_path.string().c_str())) {
					if (ImGui::MenuItem("Open")) {
						// Handle the open action
						currentDirectory = folder_path;
						strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
					}
					if (ImGui::MenuItem("Delete")) {
						// Handle the delete action
						// fs::remove(folder_path);
					}
					if (ImGui::MenuItem("Rename")) {
						// Handle the rename action
						// Add your rename logic here
					}
					if (ImGui::MenuItem("Properties")) {
						startScanTime = std::chrono::steady_clock::now();
						showProperties = true;
						propertiesPath = folder_path;
						StartGetFileSize(propertiesPath.string());
					}
					ImGui::EndPopup();
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", fileTimeToString(folder.last_changed).c_str());

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("File Folder");

				ImGui::TableSetColumnIndex(3);

				resultsMutex.lock();
				auto it = FolderSizeCache.find(folder.path);
				if (it != FolderSizeCache.end()) {
					ImGui::Text("%s", FormatFileSize(it->second));
				}
				else {
					ImGui::Text("");
				}
				resultsMutex.unlock();


				if (lastClickedPath == folder_path)
				{
					ImGui::PopStyleColor();
				}


			}

			if (sortSpecs && sortSpecs->Specs->SortDirection == ImGuiSortDirection_Descending)
				goto end;

			{
				bool* temp = new bool(false);
				ImGui::Selectable("", temp, ImGuiSelectableFlags_SpanAllColumns);
			}

files:
			for (const auto& file : FileCache.first)
			{
				fs::path file_path = file.path;
				bool* selected = new bool(false);

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);

				if (lastClickedPath == file_path)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.39f, 0.78f, 1.00f));
				}

				if (ImGui::Selectable(file.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					double currentTime = ImGui::GetTime();
					if (currentTime - lastClickTime < 0.5) { // Check if it's a double-click
						if (file_path == lastClickedPath)
						{
							OpenFile(file_path);
						}
					}
					lastClickedPath = file_path;
					lastClickTime = currentTime;

				}

				if (ImGui::BeginPopupContextItem(file_path.string().c_str())) {
					if (ImGui::MenuItem("Open")) {
						// Handle the open action
						currentDirectory = file_path;
						strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
					}
					if (ImGui::MenuItem("Delete")) {
						// Handle the delete action
						// fs::remove(folder_path);
					}
					if (ImGui::MenuItem("Rename")) {
						// Handle the rename action
						// Add your rename logic here
					}
					if (ImGui::MenuItem("Properties")) {
						showProperties = true;
						propertiesPath = file_path;
					}
					ImGui::EndPopup();
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", fileTimeToString(file.last_changed).c_str());

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", file.type.c_str());

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%s", FormatFileSize(file.size).c_str());


				if (lastClickedPath == file_path)
				{
					ImGui::PopStyleColor();
				}

			}

			if (sortSpecs && sortSpecs->Specs->SortDirection == ImGuiSortDirection_Descending)
			{
				{
					bool* temp = new bool(false);
					ImGui::Selectable("", temp, ImGuiSelectableFlags_SpanAllColumns);
				}
				goto folders;
			}

end:

			ImGui::EndTable();
		}
	}

	static void DrawSettingsWindow()
	{
		ImGui::Begin("Settings", &settingsWindow);
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		ImGui::SeparatorText("Search");
		ImGui::Checkbox("Get folder size on search", &getFolderSizeOnSearch);
		ImGui::Checkbox("Show Results while searching", &displayResultsWhileSearching);
		ImGui::InputInt("Max Search Depth", &searchDepthMax);

		ImGui::End();
	}

	static void DrawExplorer()
	{
		if (ImGui::ArrowButton("GoBack", ImGuiDir_Left))
		{
			if (!currentDirectory.empty())
			{
				GoBack();
				strcpy_s(pathQuery, sizeof(pathQuery), currentDirectory.string().c_str());
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Copy"))
			ImGui::SetClipboardText(currentDirectory.string().c_str());

		ImGui::SameLine();
		if (ImGui::Button("Settings"))
			settingsWindow = true;

		if (isSearching && activeThreads == 0)
		{
			resultSpecs->SpecsDirty = true;
			isSearching = false;
		}

		if (!isSearching)
			startSearchTime = std::chrono::steady_clock::now();

		ImGui::SameLine();
		/*float prc = bytesRead / totalUsedDiskSpace;*/
		ULONGLONG bytes_read = bytesRead;

		//float bytes_read_float = static_cast<float>(bytes_read);
		//float total_used_disk_space_float = static_cast<float>(totalUsedDiskSpace);
		float prc = ((float)bytes_read / (float)totalUsedDiskSpace) * 100.f;

		if (isSearching)
		{
			resultSpecs->SpecsDirty = true;
			elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startSearchTime);

			if (results2.size() > MAX_RESULTS)
			{
				cancelSearch = true;
			}
		}

		std::stringstream prc_ss; prc_ss << std::fixed << std::setprecision(2) << prc;
		std::stringstream ss; ss << std::fixed << std::setprecision(3) << (float)elapsedTime.count() / 1000.f;

		float framePad = ImGui::GetStyle().FramePadding.x * 2.0f;

		std::string progress_string = FormatFileSize(bytes_read) + "/" + formattedTotalUsedDiskSpace + " (" + prc_ss.str() + "%)";
		std::string depthString = " Result Count: ";
		float textWidth = ImGui::CalcTextSize((progress_string + depthString).c_str()).x + framePad;

		//if (!showingResults && results.size() != 0)
		if (!showingResults && results2.size() != 0)
		{
			float buttonWidth = ImGui::CalcTextSize("Return to results").x + framePad;
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (textWidth * 2.f) - buttonWidth * 1.2f);
			if (ImGui::Button("Return to results"))
				showingResults = true;
			ImGui::SameLine();
		}

		std::string full_progress_str = "Elapsed Time: " + ss.str() + "s " + progress_string + depthString + std::to_string(results2.size());
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - textWidth * 2.f);
		ImGui::Text("%s", full_progress_str.c_str());

		float searchWidth = 300.f;
		const char* searchLabel = "Search";
		float buttonWidth = ImGui::CalcTextSize(searchLabel).x + framePad;

		float second_width = ImGui::GetWindowWidth() - searchWidth - buttonWidth * 2.3f;

#if 0
		ImGui::Text("Path: %s", currentDirectory.string().c_str());
#else
		ImGui::Text("Path:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(second_width * 0.9f);
		ImGui::InputText("", pathQuery, 256);

		if (!isSearching)
		{
			if (pathQuery != currentDirectory)
			{
				currentDirectory = pathQuery;
			}
		}
#endif

		ImGui::SameLine();
			
		ImGui::SetCursorPosX(second_width);
		ImGui::SetNextItemWidth(searchWidth);
		ImGui::InputText("search", searchQuery, 256);

		ImGui::SameLine();
		const char* buttonLabel = isSearching ? "Cancel" : "Search";
		if (ImGui::Button(buttonLabel))
		{
			if (isSearching)
			{
				cancelSearch = true;
				isSearching = false;
			}
			else
			{
				cancelSearch = false;
				startSearchTime = std::chrono::steady_clock::now();
				Search(searchQuery);
			}
		}

		if (!showingResults)
			DrawFiles(currentDirectory);
		else
			DrawResults();

		ShowPropertiesWindow(propertiesPath);

		if (settingsWindow) {
			DrawSettingsWindow();
		}
	}


	bool Init()
	{
		showProperties = false;

		GetDriveInformation();

		LoadFonts();

		formattedTotalUsedDiskSpace = FormatFileSize(totalUsedDiskSpace);


		return true;
	}
}