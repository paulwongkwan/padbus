// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Nefarius Software Solutions e.U. and contributors
//
// Unsigned developer installer for the PadBus driver built from this fork.
//
// One file, no dependencies: the driver package (PadBus.sys, PadBus.inf and
// PadBus.cat) is embedded as resources, written to a scratch directory and
// handed to pnputil. The build is unsigned, so Windows 11 only loads it with
// test signing enabled; run with /extract to pull the package out without
// touching the driver store.
//
// Build it with build-installer.bat in this folder, after building the driver
// so bin\x64\PadBus holds the package to embed.
//
//   PadBus-dev-setup.exe                 install the driver package
//   PadBus-dev-setup.exe /extract <dir>  write the package to <dir> and stop
//   PadBus-dev-setup.exe /uninstall      remove the package from the store

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

// The requireAdministrator level comes from installer.manifest, which
// build-installer.bat embeds with mt.exe; the pragma form mangles the nested
// quotes.

namespace {

constexpr int kResSys = 101;
constexpr int kResInf = 102;
constexpr int kResCat = 103;

struct PayloadFile {
    int resource;
    const wchar_t* name;
};

const PayloadFile kPayload[] = {
    { kResSys, L"PadBus.sys" },
    { kResInf, L"PadBus.inf" },
    { kResCat, L"PadBus.cat" },
};

std::wstring ScratchDir() {
    wchar_t buffer[MAX_PATH]{};
    DWORD length = ::GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }
    return std::wstring(buffer) + L"\\PadBus-dev-setup";
}

bool WriteResource(HMODULE module, int id, const std::wstring& path) {
    HRSRC res = ::FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (res == nullptr) {
        std::fwprintf(stderr, L"missing embedded resource %d\n", id);
        return false;
    }

    HGLOBAL loaded = ::LoadResource(module, res);
    const void* data = loaded == nullptr ? nullptr : ::LockResource(loaded);
    const DWORD size = ::SizeofResource(module, res);
    if (data == nullptr || size == 0) {
        std::fwprintf(stderr, L"cannot lock embedded resource %d\n", id);
        return false;
    }

    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fwprintf(stderr, L"cannot create %s (error %lu)\n", path.c_str(), ::GetLastError());
        return false;
    }

    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, data, size, &written, nullptr);
    ::CloseHandle(file);
    if (!ok || written != size) {
        std::fwprintf(stderr, L"cannot write %s (error %lu)\n", path.c_str(), ::GetLastError());
        return false;
    }
    return true;
}

int RunAndWait(const std::wstring& command) {
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                          &startup, &process)) {
        std::fwprintf(stderr, L"cannot start command (error %lu)\n", ::GetLastError());
        return 1;
    }

    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(process.hProcess, &code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

void PrintUsage() {
    std::fwprintf(stdout,
                  L"PadBus developer build installer (UNSIGNED)\n"
                  L"\n"
                  L"  PadBus-dev-setup.exe                 install the driver package\n"
                  L"  PadBus-dev-setup.exe /extract <dir>  write the package to <dir> and stop\n"
                  L"  PadBus-dev-setup.exe /uninstall      remove the package from the driver store\n"
                  L"\n"
                  L"This build is not code-signed. Windows 11 only loads it when test signing\n"
                  L"is enabled (bcdedit /set testsigning on, Secure Boot off, reboot).\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const HINSTANCE module = ::GetModuleHandleW(nullptr);

    bool extract_only = false;
    bool uninstall = false;
    std::wstring extract_dir;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"/?" || arg == L"-h" || arg == L"--help") {
            PrintUsage();
            return 0;
        }
        if (arg == L"/extract") {
            extract_only = true;
            if (i + 1 < argc) {
                extract_dir = argv[++i];
            }
            continue;
        }
        if (arg == L"/uninstall") {
            uninstall = true;
            continue;
        }
        std::fwprintf(stderr, L"unknown argument: %s\n", arg.c_str());
        PrintUsage();
        return 2;
    }

    if (uninstall) {
        return RunAndWait(L"pnputil.exe /delete-driver PadBus.inf /uninstall /force");
    }

    const std::wstring dir = extract_only && !extract_dir.empty() ? extract_dir : ScratchDir();
    ::CreateDirectoryW(dir.c_str(), nullptr);

    for (const PayloadFile& file : kPayload) {
        if (!WriteResource(module, file.resource, dir + L"\\" + file.name)) {
            return 1;
        }
    }

    const std::wstring inf = dir + L"\\PadBus.inf";
    if (extract_only) {
        std::wprintf(L"package written to %s\n", dir.c_str());
        return 0;
    }

    std::wprintf(L"installing %s\n", inf.c_str());
    const int code = RunAndWait(L"pnputil.exe /add-driver \"" + inf + L"\" /install");
    if (code != 0) {
        std::fwprintf(stderr,
                      L"\npnputil failed with %d.\n"
                      L"An unsigned kernel driver cannot load on Windows 11 outside test mode:\n"
                      L"  bcdedit /set testsigning on   (Secure Boot must be off)\n"
                      L"  reboot, then run this installer again\n",
                      code);
        return code;
    }

    std::wprintf(L"driver package installed. The device appears only after the bus is created.\n");
    return 0;
}
