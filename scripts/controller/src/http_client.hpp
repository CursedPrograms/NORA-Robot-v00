// Blocking HTTP GET over WinHTTP -- ships with Windows, no extra
// dependency to install. This port is WiFi/Windows-only for now; a
// libcurl backend for Linux/macOS (and a Bluetooth serial transport,
// matching the Python controller's BtLink) would be natural follow-ups
// but aren't implemented here.
#pragma once

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace nora {

inline std::wstring widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

// Fire-and-forget-friendly: always returns quickly (timeout_ms) and never
// throws. Returns true + fills out_body only on a 2xx response.
inline bool http_get(const std::string& host, int port, const std::string& path,
                      std::string& out_body, int timeout_ms = 1500) {
    out_body.clear();

    HINTERNET hSession = WinHttpOpen(L"NORA-Controller/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET hConnect = WinHttpConnect(hSession, widen(host).c_str(),
                                         static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", widen(path).c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(hRequest, nullptr);

    if (ok) {
        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
            WINHTTP_NO_HEADER_INDEX);

        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, &chunk[0], avail, &read)) break;
            chunk.resize(read);
            out_body += chunk;
        }
        ok = (statusCode >= 200 && statusCode < 300);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

}  // namespace nora

#endif  // _WIN32
