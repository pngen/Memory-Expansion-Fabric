#pragma once
// Windows process helpers used by the multiprocess proofs (unattended).
#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  undef ERROR
#endif
#include <string>

namespace procutil {

#ifdef _WIN32
inline std::string exeDir() {
    char buf[4096];
    ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    std::string p = buf;
    auto slash = p.find_last_of("\\/");
    return p.substr(0, slash);
}

inline HANDLE spawnProcess(const std::string& cmdline) {
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    char* cmd = const_cast<char*>(cmdline.c_str());
    BOOL ok = ::CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                               nullptr, nullptr, &si, &pi);
    if (!ok) return nullptr;
    ::CloseHandle(pi.hThread);
    return pi.hProcess;
}

inline void killProcess(HANDLE h) { if (h) ::TerminateProcess(h, 1); }
inline std::uint64_t pidOf(HANDLE h) { return h ? (std::uint64_t)::GetProcessId(h) : 0; }
inline bool exited(HANDLE h) { return h && ::WaitForSingleObject(h, 0) == WAIT_OBJECT_0; }
inline void waitExit(HANDLE h) { if (h) ::WaitForSingleObject(h, INFINITE); }
inline void closeHandle(HANDLE h) { if (h) ::CloseHandle(h); }

inline std::uint16_t pickFreePort() {
    static bool started = false;
    if (!started) { WSADATA wsa; if (::WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 0; started = true; }
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = 0;
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
        sockaddr_in out{}; int outlen = sizeof(out);
        ::getsockname(s, (sockaddr*)&out, &outlen);
        ::closesocket(s);
        return ntohs(out.sin_port);
    }
    ::closesocket(s);
    return 0;
}
#endif

} // namespace procutil
