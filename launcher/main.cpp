#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::wstring GetExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("Could not resolve the launcher executable path.");
    }
    return std::wstring(buffer.data(), length);
}

bool FileExists(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring QuoteArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    size_t backslashCount = 0;

    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashCount;
            continue;
        }

        if (character == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashCount = 0;
            continue;
        }

        quoted.append(backslashCount, L'\\');
        backslashCount = 0;
        quoted.push_back(character);
    }

    quoted.append(backslashCount * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring BuildCommandLine(const std::vector<std::wstring>& arguments) {
    std::wstring commandLine;
    for (const std::wstring& argument : arguments) {
        if (!commandLine.empty()) {
            commandLine.push_back(L' ');
        }
        commandLine.append(QuoteArgument(argument));
    }
    return commandLine;
}

bool EqualNoCase(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

using EnvironmentEntries = std::vector<std::pair<std::wstring, std::wstring>>;

EnvironmentEntries ReadEnvironment() {
    LPWCH rawEnvironment = GetEnvironmentStringsW();
    if (rawEnvironment == nullptr) {
        throw std::runtime_error("Could not read the current process environment.");
    }

    EnvironmentEntries entries;
    for (LPWCH current = rawEnvironment; *current != L'\0';) {
        const std::wstring entry(current);
        const size_t separator = entry.find(L'=');
        if (separator != std::wstring::npos) {
            entries.emplace_back(
                entry.substr(0, separator),
                entry.substr(separator + 1));
        }
        current += entry.size() + 1;
    }

    FreeEnvironmentStringsW(rawEnvironment);
    return entries;
}

std::wstring FindEnvironmentValue(
    const EnvironmentEntries& entries,
    const std::wstring& name) {
    for (const auto& [key, value] : entries) {
        if (EqualNoCase(key, name)) {
            return value;
        }
    }
    return {};
}

void SetEnvironmentValue(
    EnvironmentEntries& entries,
    const std::wstring& name,
    const std::wstring& value) {
    for (auto& [key, currentValue] : entries) {
        if (EqualNoCase(key, name)) {
            currentValue = value;
            return;
        }
    }
    entries.emplace_back(name, value);
}

std::wstring BuildEnvironmentBlock(
    const fs::path& projectRoot,
    const fs::path& nodeRoot) {
    EnvironmentEntries entries = ReadEnvironment();

    const std::wstring existingPath = FindEnvironmentValue(entries, L"PATH");
    std::wstring portablePath =
        nodeRoot.wstring() + L";" +
        (projectRoot / L"node_modules" / L".bin").wstring();
    if (!existingPath.empty()) {
        portablePath += L";" + existingPath;
    }

    SetEnvironmentValue(entries, L"PATH", portablePath);
    SetEnvironmentValue(entries, L"DSH_HOME", (projectRoot / L".data" / L"dsh").wstring());
    SetEnvironmentValue(entries, L"NPM_CONFIG_CACHE", (projectRoot / L".cache" / L"npm").wstring());
    SetEnvironmentValue(entries, L"NPM_CONFIG_PREFIX", (projectRoot / L".runtime" / L"npm-global").wstring());
    SetEnvironmentValue(entries, L"NPM_CONFIG_USERCONFIG", (projectRoot / L".npmrc").wstring());
    SetEnvironmentValue(entries, L"NPM_CONFIG_UPDATE_NOTIFIER", L"false");

    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& left, const auto& right) {
            return _wcsicmp(left.first.c_str(), right.first.c_str()) < 0;
        });

    std::wstring block;
    for (const auto& [key, value] : entries) {
        block.append(key);
        block.push_back(L'=');
        block.append(value);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::wstring GetPowerShellPath() {
    std::vector<wchar_t> systemDirectory(MAX_PATH);
    const UINT length = GetSystemDirectoryW(
        systemDirectory.data(),
        static_cast<UINT>(systemDirectory.size()));
    if (length == 0 || length >= systemDirectory.size()) {
        return L"powershell.exe";
    }

    const fs::path windowsPowerShell =
        fs::path(systemDirectory.data()) /
        L"WindowsPowerShell" /
        L"v1.0" /
        L"powershell.exe";
    return FileExists(windowsPowerShell) ? windowsPowerShell.wstring() : L"powershell.exe";
}

DWORD RunProcess(
    const fs::path& application,
    const std::vector<std::wstring>& arguments,
    const fs::path& workingDirectory,
    const std::wstring* environmentBlock) {
    std::vector<std::wstring> commandArguments;
    commandArguments.reserve(arguments.size() + 1);
    commandArguments.push_back(application.wstring());
    commandArguments.insert(
        commandArguments.end(),
        arguments.begin(),
        arguments.end());

    std::wstring commandLine = BuildCommandLine(commandArguments);
    std::vector<wchar_t> mutableCommandLine(
        commandLine.begin(),
        commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        application.c_str(),
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_UNICODE_ENVIRONMENT,
        environmentBlock == nullptr
            ? nullptr
            : const_cast<wchar_t*>(environmentBlock->c_str()),
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo);
    if (!created) {
        return GetLastError();
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        exitCode = GetLastError();
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return exitCode;
}

bool EnsureHarnessInstalled(
    const fs::path& projectRoot,
    bool forceInstall) {
    const fs::path bootstrapScript = projectRoot / L"scripts" / L"dsh.ps1";
    const fs::path nodeExe = projectRoot / L".runtime" / L"node" / L"node.exe";
    const fs::path dshCli =
        projectRoot /
        L"node_modules" /
        L"@deepseek-ai" /
        L"dsh" /
        L"lib" /
        L"bin.js";
    const fs::path electronExe =
        projectRoot /
        L"node_modules" /
        L"electron" /
        L"dist" /
        L"electron.exe";
    const fs::path electronMain = projectRoot / L"electron" / L"main.cjs";

    if (!forceInstall &&
        FileExists(dshCli) &&
        FileExists(electronExe) &&
        FileExists(electronMain)) {
        return true;
    }

    if (!FileExists(bootstrapScript)) {
        std::wcerr << L"Missing bootstrap script: " << bootstrapScript.wstring() << L'\n';
        return false;
    }

    std::wcerr << L"First run: preparing Electron, portable Node.js, and Harness dependencies...\n";
    const DWORD exitCode = RunProcess(
        GetPowerShellPath(),
        {
            L"-NoLogo",
            L"-NoProfile",
            L"-ExecutionPolicy",
            L"Bypass",
            L"-File",
            bootstrapScript.wstring(),
            L"-InstallOnly"
        },
        projectRoot,
        nullptr);
    if (exitCode != 0) {
        std::wcerr << L"Bootstrap failed with exit code " << exitCode << L".\n";
        return false;
    }

    return FileExists(dshCli) &&
        FileExists(electronExe) &&
        FileExists(electronMain);
}

} // namespace

int main() {
    try {
        int argumentCount = 0;
        LPWSTR* rawArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (rawArguments == nullptr) {
            std::wcerr << L"Could not parse launcher arguments.\n";
            return 1;
        }

        std::vector<std::wstring> harnessArguments;
        for (int index = 1; index < argumentCount; ++index) {
            harnessArguments.emplace_back(rawArguments[index]);
        }
        LocalFree(rawArguments);

        const bool installOnly =
            !harnessArguments.empty() &&
            harnessArguments.front() == L"--launcher-install-only";
        const bool cliMode =
            !harnessArguments.empty() &&
            harnessArguments.front() == L"--cli";

        const fs::path executablePath = GetExecutablePath();
        const fs::path executableDirectory = executablePath.parent_path();
        const fs::path projectRoot = FileExists(executableDirectory / L"package.json")
            ? executableDirectory
            : executableDirectory.parent_path();
        const fs::path nodeExe = projectRoot / L".runtime" / L"node" / L"node.exe";
        const fs::path dshCli =
            projectRoot /
            L"node_modules" /
            L"@deepseek-ai" /
            L"dsh" /
            L"lib" /
            L"bin.js";
        const fs::path electronExe =
            projectRoot /
            L"node_modules" /
            L"electron" /
            L"dist" /
            L"electron.exe";
        const fs::path electronMain = projectRoot / L"electron" / L"main.cjs";

        if (!EnsureHarnessInstalled(projectRoot, installOnly)) {
            return 1;
        }

        if (installOnly) {
            return 0;
        }

        const std::wstring environmentBlock =
            BuildEnvironmentBlock(projectRoot, nodeExe.parent_path());

        if (cliMode) {
            if (!FileExists(nodeExe)) {
                std::wcerr << L"CLI mode requires .runtime\\node\\node.exe.\n";
                return 1;
            }
            if (harnessArguments.size() > 0) {
                harnessArguments.erase(harnessArguments.begin());
            }
            if (harnessArguments.empty()) {
                harnessArguments.push_back(L"web");
            }

            std::vector<std::wstring> nodeArguments;
            nodeArguments.reserve(harnessArguments.size() + 1);
            nodeArguments.push_back(dshCli.wstring());
            nodeArguments.insert(
                nodeArguments.end(),
                harnessArguments.begin(),
                harnessArguments.end());

            return static_cast<int>(RunProcess(
                nodeExe,
                nodeArguments,
                projectRoot,
                &environmentBlock));
        }

        std::vector<std::wstring> electronArguments;
        electronArguments.reserve(harnessArguments.size() + 1);
        electronArguments.push_back(electronMain.wstring());
        electronArguments.insert(
            electronArguments.end(),
            harnessArguments.begin(),
            harnessArguments.end());

        return static_cast<int>(RunProcess(
            electronExe,
            electronArguments,
            projectRoot,
            &environmentBlock));
    }
    catch (const std::exception& error) {
        std::cerr << "Launcher error: " << error.what() << '\n';
        return 1;
    }
}
