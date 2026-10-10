#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using sword = int;
using sb4 = int;
using ub4 = unsigned int;
using ub2 = unsigned short;
using OraText = unsigned char;
using OCIStmt = void;
using OCIError = void;
using OCISvcCtx = void;

extern "C" sword __cdecl HookOCIStmtPrepare(OCIStmt*, OCIError*, const OraText*, ub4, ub4, ub4);
extern "C" sword __cdecl HookOCIStmtPrepare2(OCISvcCtx*, OCIStmt**, OCIError*, const OraText*, ub4, const OraText*, ub4, ub4, ub4);
extern "C" sword __cdecl HookOCIBindByName(OCIStmt*, void**, OCIError*, const OraText*, sb4, void*, sb4, ub2, void*, ub2*, ub2*, ub4, ub4*, ub4);
extern "C" sword __cdecl HookOCIBindByPos(OCIStmt*, void**, OCIError*, ub4, void*, sb4, ub2, void*, ub2*, ub2*, ub4, ub4*, ub4);
extern "C" sword __cdecl HookOCIBindByName2(OCIStmt*, void**, OCIError*, const OraText*, sb4, void*, long long, ub2, void*, ub4*, ub2*, ub4, ub4*, ub4);
extern "C" sword __cdecl HookOCIBindByPos2(OCIStmt*, void**, OCIError*, ub4, void*, long long, ub2, void*, ub4*, ub2*, ub4, ub4*, ub4);
extern "C" sword __cdecl HookOCIStmtExecute(OCISvcCtx*, OCIStmt*, OCIError*, ub4, ub4, const void*, void*, ub4);
extern "C" sword __cdecl HookOCITransCommit(OCISvcCtx*, OCIError*, ub4);
extern "C" sword __cdecl HookOCITransRollback(OCISvcCtx*, OCIError*, ub4);

namespace
{
constexpr const wchar_t* PipePrefix = L"\\\\.\\pipe\\sqltracker-oci-";
constexpr const char* AgentVersion = "0.1.0";
constexpr size_t MaxQueuedEvents = 8192;
constexpr size_t DetailedEventBacklogThreshold = MaxQueuedEvents * 3 / 4;

using OCIStmtPrepareFn = sword(__cdecl*)(OCIStmt*, OCIError*, const OraText*, ub4, ub4, ub4);
using OCIStmtPrepare2Fn = sword(__cdecl*)(OCISvcCtx*, OCIStmt**, OCIError*, const OraText*, ub4, const OraText*, ub4, ub4, ub4);
using OCIBindByNameFn = sword(__cdecl*)(OCIStmt*, void**, OCIError*, const OraText*, sb4, void*, sb4, ub2, void*, ub2*, ub2*, ub4, ub4*, ub4);
using OCIBindByPosFn = sword(__cdecl*)(OCIStmt*, void**, OCIError*, ub4, void*, sb4, ub2, void*, ub2*, ub2*, ub4, ub4*, ub4);
using OCIBindByName2Fn = sword(__cdecl*)(OCIStmt*, void**, OCIError*, const OraText*, sb4, void*, long long, ub2, void*, ub4*, ub2*, ub4, ub4*, ub4);
using OCIBindByPos2Fn = sword(__cdecl*)(OCIStmt*, void**, OCIError*, ub4, void*, long long, ub2, void*, ub4*, ub2*, ub4, ub4*, ub4);
using OCIStmtExecuteFn = sword(__cdecl*)(OCISvcCtx*, OCIStmt*, OCIError*, ub4, ub4, const void*, void*, ub4);
using OCITransCommitFn = sword(__cdecl*)(OCISvcCtx*, OCIError*, ub4);
using OCITransRollbackFn = sword(__cdecl*)(OCISvcCtx*, OCIError*, ub4);
using OCIErrorGetFn = sword(__cdecl*)(void*, ub4, OraText*, sb4*, OraText*, ub4, ub4);

struct BindCapture
{
    int index = 0;
    std::string name;
    void* value = nullptr;
    sb4 valueSize = 0;
    ub2 dataType = 0;
    OCIError* error = nullptr;
    ub2* actualLength = nullptr;
    ub4* actualLength32 = nullptr;
};

struct StatementState
{
    std::string sql;
    std::vector<BindCapture> binds;
    std::string lastExecuteSignature;
    ULONGLONG lastExecuteTick = 0;
};

struct StatementSnapshot
{
    std::string sql;
    std::vector<BindCapture> binds;
};

struct PatchedImport
{
    void** slot = nullptr;
    void* original = nullptr;
    void* hook = nullptr;
};

struct InlineHook
{
    void* target = nullptr;
    void* hook = nullptr;
    void* trampoline = nullptr;
    std::uint8_t original[32]{};
    size_t patchSize = 0;
};

HMODULE g_module = nullptr;
HANDLE g_pipe = INVALID_HANDLE_VALUE;
HANDLE g_writerThread = nullptr;
CRITICAL_SECTION g_pipeLock{};
CRITICAL_SECTION g_queueLock{};
CRITICAL_SECTION g_stateLock{};
CONDITION_VARIABLE g_queueAvailable{};
LONG64 g_sequence = 0;
volatile LONG g_unloading = 0;
volatile LONG g_writerStarted = 0;
volatile LONG g_droppedEvents = 0;

OCIStmtPrepareFn g_OCIStmtPrepare = nullptr;
OCIStmtPrepare2Fn g_OCIStmtPrepare2 = nullptr;
OCIBindByNameFn g_OCIBindByName = nullptr;
OCIBindByPosFn g_OCIBindByPos = nullptr;
OCIBindByName2Fn g_OCIBindByName2 = nullptr;
OCIBindByPos2Fn g_OCIBindByPos2 = nullptr;
OCIStmtExecuteFn g_OCIStmtExecute = nullptr;
OCITransCommitFn g_OCITransCommit = nullptr;
OCITransRollbackFn g_OCITransRollback = nullptr;
OCIErrorGetFn g_OCIErrorGet = nullptr;

std::unordered_map<void*, StatementState> g_statements;
std::deque<std::string> g_eventQueue;
std::vector<PatchedImport> g_patches;
std::vector<InlineHook> g_inlineHooks;
std::string g_lastExecuteSignature;
ULONGLONG g_lastExecuteTick = 0;

std::string PtrToHex(const void* value)
{
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%p", value);
    return buffer;
}

std::string JsonEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (unsigned char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                char buffer[8]{};
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                escaped += buffer;
            }
            else
            {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    return escaped;
}

std::string QuoteJson(const std::string& value)
{
    return "\"" + JsonEscape(value) + "\"";
}

bool IsPlainAsciiText(const OraText* bytes, ub4 length)
{
    if (bytes == nullptr || length == 0)
    {
        return false;
    }

    for (ub4 index = 0; index < length; ++index)
    {
        const auto ch = bytes[index];
        if (ch == 0)
        {
            return false;
        }

        if (ch == '\r' || ch == '\n' || ch == '\t')
        {
            continue;
        }

        if (ch < 0x20 || ch > 0x7E)
        {
            return false;
        }
    }

    return true;
}

bool ContainsZeroByte(const OraText* bytes, ub4 length)
{
    if (bytes == nullptr)
    {
        return false;
    }

    for (ub4 index = 0; index < length; ++index)
    {
        if (bytes[index] == 0)
        {
            return true;
        }
    }

    return false;
}

bool LooksLikeUtf16Le(const OraText* bytes, ub4 length)
{
    if (bytes == nullptr || length < 4 || (length % 2) != 0)
    {
        return false;
    }

    while (length >= 2 && bytes[length - 2] == 0 && bytes[length - 1] == 0)
    {
        length -= 2;
    }

    if (length < 4)
    {
        return false;
    }

    if (IsPlainAsciiText(bytes, length))
    {
        return false;
    }

    const ub4 sampleLength = std::min<ub4>(length, 256);
    ub4 oddZeroCount = 0;
    ub4 pairCount = 0;
    ub4 readableCount = 0;
    ub4 cjkCount = 0;
    ub4 highByteCount = 0;

    for (ub4 index = 0; index + 1 < sampleLength; index += 2)
    {
        if (bytes[index + 1] == 0)
        {
            ++oddZeroCount;
        }

        if (bytes[index] >= 0x80 || bytes[index + 1] >= 0x80)
        {
            ++highByteCount;
        }

        const wchar_t ch = static_cast<wchar_t>(bytes[index] | (bytes[index + 1] << 8));
        if (ch == L'\0' && pairCount > 0)
        {
            break;
        }

        if ((ch >= 0x20 && ch <= 0x7E) ||
            (ch >= 0x4E00 && ch <= 0x9FFF) ||
            ch == L'\r' ||
            ch == L'\n' ||
            ch == L'\t')
        {
            ++readableCount;
        }

        if (ch >= 0x4E00 && ch <= 0x9FFF)
        {
            ++cjkCount;
        }

        ++pairCount;
    }

    if (pairCount == 0)
    {
        return false;
    }

    const bool asciiUtf16 = oddZeroCount * 2 >= pairCount;
    const bool readableUtf16 = readableCount * 100 >= pairCount * 80;
    return readableUtf16 && (asciiUtf16 || (highByteCount > 0 && cjkCount > 0));
}

std::string Utf16LeToUtf8(const OraText* bytes, ub4 length)
{
    while (length >= 2 && bytes[length - 2] == 0 && bytes[length - 1] == 0)
    {
        length -= 2;
    }

    auto* wide = reinterpret_cast<const wchar_t*>(bytes);
    int wideLength = static_cast<int>(length / sizeof(wchar_t));
    while (wideLength > 0 && wide[wideLength - 1] == L'\0')
    {
        --wideLength;
    }

    if (wideLength <= 0)
    {
        return {};
    }

    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide, wideLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, wideLength, utf8.data(), utf8Length, nullptr, nullptr);
    return utf8;
}

ub4 NullTerminatedUtf16LeLength(const OraText* bytes, ub4 maxLength)
{
    if (bytes == nullptr || maxLength < 2)
    {
        return 0;
    }

    const ub4 evenMax = maxLength - (maxLength % 2);
    for (ub4 index = 0; index + 1 < evenMax; index += 2)
    {
        if (bytes[index] == 0 && bytes[index + 1] == 0)
        {
            return index;
        }
    }

    return evenMax;
}

bool IsValidUtf8(const OraText* bytes, ub4 length)
{
    ub4 index = 0;
    while (index < length)
    {
        const unsigned char ch = bytes[index];
        if (ch <= 0x7F)
        {
            ++index;
            continue;
        }

        ub4 extraBytes = 0;
        if ((ch & 0xE0) == 0xC0)
        {
            extraBytes = 1;
            if (ch < 0xC2)
            {
                return false;
            }
        }
        else if ((ch & 0xF0) == 0xE0)
        {
            extraBytes = 2;
        }
        else if ((ch & 0xF8) == 0xF0)
        {
            extraBytes = 3;
            if (ch > 0xF4)
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        if (index + extraBytes >= length)
        {
            return false;
        }

        for (ub4 offset = 1; offset <= extraBytes; ++offset)
        {
            if ((bytes[index + offset] & 0xC0) != 0x80)
            {
                return false;
            }
        }

        index += extraBytes + 1;
    }

    return true;
}

std::string BytesToUtf8(const OraText* bytes, ub4 length)
{
    if (bytes == nullptr || length == 0)
    {
        return {};
    }

    if (IsPlainAsciiText(bytes, length))
    {
        return std::string(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(bytes) + length);
    }

    if (!ContainsZeroByte(bytes, length) && IsValidUtf8(bytes, length))
    {
        return std::string(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(bytes) + length);
    }

    if (LooksLikeUtf16Le(bytes, length))
    {
        return Utf16LeToUtf8(bytes, length);
    }

    while (length > 0 && bytes[length - 1] == '\0')
    {
        --length;
    }

    if (length == 0)
    {
        return {};
    }

    if (IsPlainAsciiText(bytes, length))
    {
        return std::string(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(bytes) + length);
    }

    if (IsValidUtf8(bytes, length))
    {
        return std::string(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(bytes) + length);
    }

    if (LooksLikeUtf16Le(bytes, length))
    {
        return Utf16LeToUtf8(bytes, length);
    }

    const int wideLength = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes), static_cast<int>(length), nullptr, 0);
    if (wideLength <= 0)
    {
        return std::string(reinterpret_cast<const char*>(bytes), reinterpret_cast<const char*>(bytes) + length);
    }

    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes), static_cast<int>(length), wide.data(), wideLength);

    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(), utf8Length, nullptr, nullptr);
    return utf8;
}

std::string OptionalOraTextToUtf8(const OraText* text, sb4 length)
{
    if (text == nullptr)
    {
        return {};
    }

    if (length < 0)
    {
        length = static_cast<sb4>(std::strlen(reinterpret_cast<const char*>(text)));
    }

    return BytesToUtf8(text, static_cast<ub4>(std::max(0, length)));
}

std::string StripLeadingColon(std::string value)
{
    if (!value.empty() && value.front() == ':')
    {
        value.erase(value.begin());
    }

    return value;
}

std::string UtcTimestamp()
{
    SYSTEMTIME now{};
    GetSystemTime(&now);

    char buffer[40]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);

    return buffer;
}

double ElapsedMilliseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& end)
{
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart == 0)
    {
        return 0.0;
    }

    return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

void ClosePipe();

bool IsPipeConnected()
{
    EnterCriticalSection(&g_pipeLock);
    const bool connected = g_pipe != INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&g_pipeLock);
    return connected;
}

bool IsEventQueueBacklogged(size_t threshold)
{
    EnterCriticalSection(&g_queueLock);
    const bool backlogged = g_eventQueue.size() >= threshold;
    LeaveCriticalSection(&g_queueLock);
    return backlogged;
}

DWORD WINAPI PipeWriterThread(LPVOID)
{
    while (true)
    {
        std::string line;
        EnterCriticalSection(&g_queueLock);
        while (g_eventQueue.empty() && InterlockedCompareExchange(&g_unloading, 0, 0) == 0)
        {
            SleepConditionVariableCS(&g_queueAvailable, &g_queueLock, INFINITE);
        }

        if (g_eventQueue.empty() && InterlockedCompareExchange(&g_unloading, 0, 0) != 0)
        {
            LeaveCriticalSection(&g_queueLock);
            break;
        }

        line = std::move(g_eventQueue.front());
        g_eventQueue.pop_front();
        LeaveCriticalSection(&g_queueLock);

        HANDLE pipe = INVALID_HANDLE_VALUE;
        EnterCriticalSection(&g_pipeLock);
        pipe = g_pipe;
        LeaveCriticalSection(&g_pipeLock);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        DWORD written = 0;
        const BOOL ok = WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        if (!ok || written != static_cast<DWORD>(line.size()))
        {
            ClosePipe();
        }
    }

    InterlockedExchange(&g_writerStarted, 0);
    return 0;
}

void StartPipeWriter()
{
    if (InterlockedCompareExchange(&g_writerStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE thread = CreateThread(nullptr, 0, PipeWriterThread, nullptr, 0, nullptr);
    if (thread == nullptr)
    {
        InterlockedExchange(&g_writerStarted, 0);
        return;
    }

    g_writerThread = thread;
}

void StopPipeWriter()
{
    WakeAllConditionVariable(&g_queueAvailable);

    HANDLE thread = g_writerThread;
    if (thread != nullptr)
    {
        WaitForSingleObject(thread, 1000);
        CloseHandle(thread);
        g_writerThread = nullptr;
    }

    EnterCriticalSection(&g_queueLock);
    g_eventQueue.clear();
    LeaveCriticalSection(&g_queueLock);
    InterlockedExchange(&g_writerStarted, 0);
}

bool EnqueueEvent(std::string line)
{
    if (InterlockedCompareExchange(&g_writerStarted, 0, 0) == 0 || !IsPipeConnected())
    {
        return false;
    }

    EnterCriticalSection(&g_queueLock);
    if (g_eventQueue.size() >= MaxQueuedEvents)
    {
        InterlockedIncrement(&g_droppedEvents);
        LeaveCriticalSection(&g_queueLock);
        return false;
    }

    g_eventQueue.push_back(std::move(line));
    WakeConditionVariable(&g_queueAvailable);
    LeaveCriticalSection(&g_queueLock);
    return true;
}

void SendEvent(const char* type, const std::string& payloadJson)
{
    if (InterlockedCompareExchange(&g_unloading, 0, 0) != 0)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_writerStarted, 0, 0) == 0 || !IsPipeConnected())
    {
        return;
    }

    std::ostringstream event;
    event << "{\"version\":1"
          << ",\"type\":\"" << type << "\""
          << ",\"processId\":" << GetCurrentProcessId()
          << ",\"threadId\":" << GetCurrentThreadId()
          << ",\"sequence\":" << InterlockedIncrement64(&g_sequence)
          << ",\"timestamp\":\"" << UtcTimestamp() << "\""
          << ",\"payload\":" << payloadJson
          << "}\n";

    EnqueueEvent(event.str());
}

void SendInfo(const std::string& message)
{
    SendEvent("agent_info", "{\"message\":" + QuoteJson(message) + "}");
}

void SendHello()
{
    std::ostringstream payload;
    payload << "{\"agentKind\":\"oracle-oci\""
            << ",\"agentVersion\":\"" << AgentVersion << "\""
            << ",\"processId\":" << GetCurrentProcessId()
            << ",\"bitness\":32}";

    SendEvent("agent_hello", payload.str());
}

void ConnectPipe()
{
    wchar_t pipeName[128]{};
    std::swprintf(pipeName, std::size(pipeName), L"%s%lu", PipePrefix, GetCurrentProcessId());

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        HANDLE pipe = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
        {
            EnterCriticalSection(&g_pipeLock);
            g_pipe = pipe;
            LeaveCriticalSection(&g_pipeLock);
            return;
        }

        if (GetLastError() == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeW(pipeName, 500);
        }
        else
        {
            Sleep(100);
        }
    }
}

void ClosePipe()
{
    EnterCriticalSection(&g_pipeLock);
    if (g_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_pipeLock);
}

std::string FormatFloatingPoint(double value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);

    if (std::strcmp(buffer, "-0") == 0)
    {
        return "0";
    }

    return buffer;
}

bool TryDecodeOracleNumberPayload(const unsigned char* bytes, int length, std::string& value)
{
    if (bytes == nullptr || length <= 0)
    {
        return false;
    }

    const unsigned char* payload = bytes;
    int payloadLength = length;
    if (bytes[0] > 0 && bytes[0] <= 21 && bytes[0] + 1 <= length)
    {
        payload = bytes + 1;
        payloadLength = bytes[0];
    }

    if (payloadLength <= 0)
    {
        return false;
    }

    if (payload[0] == 0x80)
    {
        value = "0";
        return true;
    }

    const bool negative = payload[0] < 0x80;
    const int exponent = negative
        ? 62 - static_cast<int>(payload[0])
        : static_cast<int>(payload[0]) - 193;

    if (exponent < -64 || exponent > 64)
    {
        return false;
    }

    std::string digits;
    for (int index = 1; index < payloadLength; ++index)
    {
        if (negative && payload[index] == 102)
        {
            break;
        }

        const int base100 = negative
            ? 101 - static_cast<int>(payload[index])
            : static_cast<int>(payload[index]) - 1;

        if (base100 < 0 || base100 > 99)
        {
            return false;
        }

        char pair[3]{};
        std::snprintf(pair, sizeof(pair), "%02d", base100);
        digits += pair;
    }

    if (digits.empty())
    {
        return false;
    }

    const int decimalPosition = (exponent + 1) * 2;
    std::string formatted;
    if (decimalPosition <= 0)
    {
        formatted = "0.";
        formatted.append(static_cast<size_t>(-decimalPosition), '0');
        formatted += digits;
    }
    else if (decimalPosition >= static_cast<int>(digits.size()))
    {
        formatted = digits;
        formatted.append(static_cast<size_t>(decimalPosition - static_cast<int>(digits.size())), '0');
    }
    else
    {
        formatted = digits.substr(0, static_cast<size_t>(decimalPosition));
        formatted.push_back('.');
        formatted += digits.substr(static_cast<size_t>(decimalPosition));
    }

    const auto dot = formatted.find('.');
    const auto integerEnd = dot == std::string::npos ? formatted.size() : dot;
    size_t leading = 0;
    while (leading + 1 < integerEnd && formatted[leading] == '0')
    {
        ++leading;
    }

    if (leading > 0)
    {
        formatted.erase(0, leading);
    }

    const auto decimal = formatted.find('.');
    if (decimal != std::string::npos)
    {
        while (!formatted.empty() && formatted.back() == '0')
        {
            formatted.pop_back();
        }

        if (!formatted.empty() && formatted.back() == '.')
        {
            formatted.pop_back();
        }
    }

    if (formatted.empty())
    {
        formatted = "0";
    }

    if (negative && formatted != "0")
    {
        formatted.insert(formatted.begin(), '-');
    }

    value = formatted;
    return true;
}

std::string FormatOracleNumber(const BindCapture& bind, int length)
{
    auto* bytes = static_cast<const unsigned char*>(bind.value);
    if (bytes == nullptr)
    {
        return "null";
    }

    auto numberLength = length;
    if (numberLength <= 0)
    {
        numberLength = bind.valueSize;
    }

    if (numberLength > 0)
    {
        numberLength = std::min(numberLength, 64);
        std::string decoded;
        if (TryDecodeOracleNumberPayload(bytes, numberLength, decoded))
        {
            return decoded;
        }
    }

    if (bind.valueSize > 0)
    {
        const auto fallbackLength = std::min(bind.valueSize, 64);
        std::string decoded;
        if (TryDecodeOracleNumberPayload(bytes, fallbackLength, decoded))
        {
            return decoded;
        }
    }

    std::ostringstream fallback;
    fallback << "[dty:" << bind.dataType << "]";
    return fallback.str();
}

std::string FormatOracleDate(const void* value, int length)
{
    if (value == nullptr || length < 7)
    {
        return "[date unavailable]";
    }

    const auto* bytes = static_cast<const unsigned char*>(value);
    const int year = (static_cast<int>(bytes[0]) - 100) * 100 + (static_cast<int>(bytes[1]) - 100);
    const int month = bytes[2];
    const int day = bytes[3];
    const int hour = static_cast<int>(bytes[4]) - 1;
    const int minute = static_cast<int>(bytes[5]) - 1;
    const int second = static_cast<int>(bytes[6]) - 1;

    char buffer[32]{};
    if (hour == 0 && minute == 0 && second == 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
    }

    return buffer;
}

std::string DisplayTextBindValue(const BindCapture& bind, int length)
{
    auto* bytes = static_cast<const OraText*>(bind.value);
    if (bytes == nullptr)
    {
        return "null";
    }

    if (length > 0)
    {
        if (LooksLikeUtf16Le(bytes, static_cast<ub4>(length)))
        {
            return Utf16LeToUtf8(bytes, static_cast<ub4>(length));
        }

        const auto doubledLength = static_cast<long long>(length) * 2;
        if (doubledLength > 0 &&
            doubledLength <= 8192 &&
            LooksLikeUtf16Le(bytes, static_cast<ub4>(doubledLength)))
        {
            return Utf16LeToUtf8(bytes, static_cast<ub4>(doubledLength));
        }
    }

    if (bind.valueSize > 0)
    {
        const auto valueLength = static_cast<ub4>(std::min<sb4>(bind.valueSize, 8192));
        const auto utf16Length = NullTerminatedUtf16LeLength(bytes, valueLength);
        if (utf16Length >= 4 && LooksLikeUtf16Le(bytes, utf16Length))
        {
            return Utf16LeToUtf8(bytes, utf16Length);
        }
    }

    return OptionalOraTextToUtf8(bytes, length);
}

std::string DisplayBindValue(const BindCapture& bind)
{
    if (bind.value == nullptr)
    {
        return "null";
    }

    int length = bind.valueSize;
    if (bind.actualLength32 != nullptr && *bind.actualLength32 > 0)
    {
        length = static_cast<int>(*bind.actualLength32);
    }
    else if (bind.actualLength != nullptr && *bind.actualLength > 0)
    {
        length = *bind.actualLength;
    }

    switch (bind.dataType)
    {
    case 1:  // SQLT_CHR
    case 5:  // SQLT_STR
    case 9:  // SQLT_VCS
    case 96: // SQLT_AFC
    case 97: // SQLT_AVC
        return DisplayTextBindValue(bind, length);
    case 2: // SQLT_NUM
    case 6: // SQLT_VNU
        return FormatOracleNumber(bind, length);
    case 3: // SQLT_INT
        if (bind.valueSize == 1)
        {
            return std::to_string(*static_cast<const signed char*>(bind.value));
        }
        if (bind.valueSize == 2)
        {
            return std::to_string(*static_cast<const short*>(bind.value));
        }
        if (bind.valueSize == 4)
        {
            return std::to_string(*static_cast<const int*>(bind.value));
        }
        break;
    case 4: // SQLT_FLT
        if (bind.valueSize == 4)
        {
            return FormatFloatingPoint(*static_cast<const float*>(bind.value));
        }
        if (bind.valueSize == 8)
        {
            return FormatFloatingPoint(*static_cast<const double*>(bind.value));
        }
        break;
    case 12: // SQLT_DAT
        return FormatOracleDate(bind.value, length);
    default:
        break;
    }

    std::ostringstream fallback;
    fallback << "[dty:" << bind.dataType << "]";
    return fallback.str();
}

void SendPrepare(void* statement, const std::string& sql)
{
    std::ostringstream payload;
    payload << "{\"statementId\":" << QuoteJson(PtrToHex(statement))
            << ",\"sql\":" << QuoteJson(sql) << "}";

    SendEvent("prepare", payload.str());
}

void SendBind(void* statement, const BindCapture& bind)
{
    if (IsEventQueueBacklogged(DetailedEventBacklogThreshold))
    {
        return;
    }

    std::ostringstream payload;
    payload << "{\"statementId\":" << QuoteJson(PtrToHex(statement))
            << ",\"index\":" << bind.index
            << ",\"name\":" << QuoteJson(bind.name)
            << ",\"dataType\":" << bind.dataType
            << ",\"displayValue\":" << QuoteJson(DisplayBindValue(bind)) << "}";

    SendEvent("bind", payload.str());
}

std::string CaptureOciError(OCIError* error, sword status, sb4* errorCode)
{
    if (errorCode != nullptr)
    {
        *errorCode = 0;
    }

    if (g_OCIErrorGet == nullptr || error == nullptr || status == 0)
    {
        return {};
    }

    OraText buffer[1024]{};
    sb4 code = 0;
    const sword errorStatus = g_OCIErrorGet(error, 1, nullptr, &code, buffer, sizeof(buffer) - 1, 2);
    if (errorCode != nullptr)
    {
        *errorCode = code;
    }

    if (errorStatus != 0 && buffer[0] == '\0')
    {
        if (code != 0)
        {
            return "ORA-" + std::to_string(-code);
        }

        return {};
    }

    ub4 length = 0;
    if (buffer[0] != 0 && buffer[1] == 0)
    {
        length = NullTerminatedUtf16LeLength(buffer, static_cast<ub4>(sizeof(buffer)));
    }
    else
    {
        length = static_cast<ub4>(std::strlen(reinterpret_cast<const char*>(buffer)));
    }

    auto message = BytesToUtf8(buffer, length);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
    {
        message.pop_back();
    }

    return message;
}

std::string BuildExecuteSignature(const std::string& sql, const std::vector<BindCapture>& binds, sword status, const std::string& errorMessage)
{
    std::ostringstream signature;
    signature << sql << "|status=" << status << "|error=" << errorMessage;
    for (const auto& bind : binds)
    {
        signature << "|";
        signature << bind.index << ":";
        signature << bind.name << "=";
        signature << DisplayBindValue(bind);
    }

    return signature.str();
}

bool ShouldSuppressExecute(void* statement, const std::string& signature, ULONGLONG nowTick)
{
    constexpr ULONGLONG DuplicateWindowMilliseconds = 100;

    EnterCriticalSection(&g_stateLock);
    auto& state = g_statements[statement];
    const bool statementDuplicate =
        state.lastExecuteSignature == signature &&
        state.lastExecuteTick != 0 &&
        nowTick >= state.lastExecuteTick &&
        nowTick - state.lastExecuteTick <= DuplicateWindowMilliseconds;

    const bool processDuplicate =
        g_lastExecuteSignature == signature &&
        g_lastExecuteTick != 0 &&
        nowTick >= g_lastExecuteTick &&
        nowTick - g_lastExecuteTick <= DuplicateWindowMilliseconds;

    const bool duplicate = statementDuplicate || processDuplicate;

    if (!duplicate)
    {
        state.lastExecuteSignature = signature;
        state.lastExecuteTick = nowTick;
        g_lastExecuteSignature = signature;
        g_lastExecuteTick = nowTick;
    }

    LeaveCriticalSection(&g_stateLock);
    return duplicate;
}

void SendExecuteCompleted(void* statement, double elapsedMilliseconds, sword status, sb4 errorCode, const std::string& errorMessage)
{
    char elapsed[64]{};
    std::snprintf(elapsed, sizeof(elapsed), "%.3f", elapsedMilliseconds);

    std::ostringstream payload;
    payload << "{\"statementId\":" << QuoteJson(PtrToHex(statement))
            << ",\"elapsedMilliseconds\":" << elapsed
            << ",\"endTime\":" << QuoteJson(UtcTimestamp())
            << ",\"status\":" << status
            << ",\"errorCode\":";

    if (errorCode == 0)
    {
        payload << "null";
    }
    else
    {
        payload << errorCode;
    }

    payload << ",\"errorMessage\":";
    if (errorMessage.empty())
    {
        payload << "null";
    }
    else
    {
        payload << QuoteJson(errorMessage);
    }

    payload << "}";

    SendEvent("execute_completed", payload.str());
}

void SendTransactionCompleted(const char* type, double elapsedMilliseconds, sword status)
{
    char elapsed[64]{};
    std::snprintf(elapsed, sizeof(elapsed), "%.3f", elapsedMilliseconds);

    std::ostringstream payload;
    payload << "{\"elapsedMilliseconds\":" << elapsed
            << ",\"endTime\":" << QuoteJson(UtcTimestamp())
            << ",\"status\":" << status
            << ",\"errorCode\":null,\"errorMessage\":null}";

    SendEvent(type, payload.str());
}

void ResetStatement(void* statement)
{
    EnterCriticalSection(&g_stateLock);
    g_statements[statement] = StatementState{};
    LeaveCriticalSection(&g_stateLock);
}

void StoreSql(void* statement, const std::string& sql)
{
    EnterCriticalSection(&g_stateLock);
    auto& state = g_statements[statement];
    state.sql = sql;
    state.binds.clear();
    state.lastExecuteSignature.clear();
    state.lastExecuteTick = 0;
    LeaveCriticalSection(&g_stateLock);
}

void StoreBind(void* statement, const BindCapture& bind)
{
    EnterCriticalSection(&g_stateLock);
    auto& state = g_statements[statement];
    auto normalized = bind;

    auto existing = std::find_if(
        state.binds.begin(),
        state.binds.end(),
        [&](const BindCapture& item)
        {
            return item.index == bind.index ||
                (!item.name.empty() && !bind.name.empty() && _stricmp(item.name.c_str(), bind.name.c_str()) == 0);
        });

    if (existing == state.binds.end())
    {
        if (normalized.index <= 0)
        {
            normalized.index = static_cast<int>(state.binds.size()) + 1;
        }

        state.binds.push_back(normalized);
    }
    else
    {
        if (normalized.index <= 0)
        {
            normalized.index = existing->index;
        }

        *existing = normalized;
    }

    LeaveCriticalSection(&g_stateLock);
}

StatementSnapshot SnapshotStatement(void* statement)
{
    EnterCriticalSection(&g_stateLock);
    StatementSnapshot snapshot;
    const auto found = g_statements.find(statement);
    if (found != g_statements.end())
    {
        snapshot.sql = found->second.sql;
        snapshot.binds = found->second.binds;
    }
    LeaveCriticalSection(&g_stateLock);
    return snapshot;
}

void* HookForImportName(const char* name)
{
    if (std::strcmp(name, "OCIStmtPrepare") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIStmtPrepare);
    }
    if (std::strcmp(name, "OCIStmtPrepare2") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIStmtPrepare2);
    }
    if (std::strcmp(name, "OCIBindByName") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIBindByName);
    }
    if (std::strcmp(name, "OCIBindByPos") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIBindByPos);
    }
    if (std::strcmp(name, "OCIBindByName2") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIBindByName2);
    }
    if (std::strcmp(name, "OCIBindByPos2") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIBindByPos2);
    }
    if (std::strcmp(name, "OCIStmtExecute") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCIStmtExecute);
    }
    if (std::strcmp(name, "OCITransCommit") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCITransCommit);
    }
    if (std::strcmp(name, "OCITransRollback") == 0)
    {
        return reinterpret_cast<void*>(&::HookOCITransRollback);
    }

    return nullptr;
}

size_t ModRmLength(const std::uint8_t* instruction, size_t offset)
{
    const std::uint8_t modrm = instruction[offset];
    const std::uint8_t mod = static_cast<std::uint8_t>((modrm >> 6) & 0x03);
    const std::uint8_t rm = static_cast<std::uint8_t>(modrm & 0x07);
    size_t length = offset + 1;

    if (mod != 3 && rm == 4)
    {
        const std::uint8_t sib = instruction[length++];
        const std::uint8_t base = static_cast<std::uint8_t>(sib & 0x07);
        if (mod == 0 && base == 5)
        {
            length += 4;
        }
    }
    else if (mod == 0 && rm == 5)
    {
        length += 4;
    }

    if (mod == 1)
    {
        length += 1;
    }
    else if (mod == 2)
    {
        length += 4;
    }

    return length;
}

size_t InstructionLength(const std::uint8_t* instruction)
{
    size_t offset = 0;
    bool hasPrefix = true;
    while (hasPrefix)
    {
        switch (instruction[offset])
        {
        case 0x26:
        case 0x2E:
        case 0x36:
        case 0x3E:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
        case 0xF0:
        case 0xF2:
        case 0xF3:
            ++offset;
            break;
        default:
            hasPrefix = false;
            break;
        }
    }

    const std::uint8_t opcode = instruction[offset++];

    if ((opcode >= 0x50 && opcode <= 0x5F) || opcode == 0x90 || opcode == 0xC3 || opcode == 0xCC)
    {
        return offset;
    }

    if (opcode >= 0xB8 && opcode <= 0xBF)
    {
        return offset + 4;
    }

    if (opcode == 0x68 || opcode == 0xA1 || opcode == 0xA3 || opcode == 0xE8 || opcode == 0xE9)
    {
        return offset + 4;
    }

    if (opcode == 0x6A || opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F))
    {
        return offset + 1;
    }

    if (opcode == 0xC2)
    {
        return offset + 2;
    }

    if (opcode == 0x0F)
    {
        const std::uint8_t second = instruction[offset++];
        if (second >= 0x80 && second <= 0x8F)
        {
            return offset + 4;
        }

        return ModRmLength(instruction, offset);
    }

    switch (opcode)
    {
    case 0x80:
    case 0x81:
        return ModRmLength(instruction, offset) + 4;
    case 0x83:
        return ModRmLength(instruction, offset) + 1;
    case 0x8B:
    case 0x89:
    case 0x8D:
    case 0x85:
    case 0xFF:
        return ModRmLength(instruction, offset);
    default:
        return offset;
    }
}

size_t CalculatePatchSize(void* target)
{
    size_t patchSize = 0;
    const auto* code = static_cast<const std::uint8_t*>(target);

    while (patchSize < 5)
    {
        const size_t instructionLength = InstructionLength(code + patchSize);
        if (instructionLength == 0 || patchSize + instructionLength > 32)
        {
            return 0;
        }

        patchSize += instructionLength;
    }

    return patchSize;
}

bool InstallInlineHook(void* target, void* hook, void** originalOut)
{
    if (target == nullptr || hook == nullptr || originalOut == nullptr)
    {
        return false;
    }

    const size_t patchSize = CalculatePatchSize(target);
    if (patchSize < 5 || patchSize > 32)
    {
        return false;
    }

    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, patchSize + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr)
    {
        return false;
    }

    auto* targetBytes = static_cast<std::uint8_t*>(target);
    std::memcpy(trampoline, targetBytes, patchSize);
    trampoline[patchSize] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + patchSize + 1) =
        reinterpret_cast<std::uint8_t*>(target) + patchSize - (trampoline + patchSize + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    InlineHook installed{};
    installed.target = target;
    installed.hook = hook;
    installed.trampoline = trampoline;
    installed.patchSize = patchSize;
    std::memcpy(installed.original, targetBytes, patchSize);

    targetBytes[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(targetBytes + 1) =
        static_cast<std::uint8_t*>(hook) - (targetBytes + 5);
    for (size_t index = 5; index < patchSize; ++index)
    {
        targetBytes[index] = 0x90;
    }

    DWORD ignored = 0;
    VirtualProtect(target, patchSize, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, patchSize);

    g_inlineHooks.push_back(installed);
    *originalOut = trampoline;
    return true;
}

void RestoreInlineHooks()
{
    for (const auto& installed : g_inlineHooks)
    {
        if (installed.target == nullptr || installed.patchSize == 0)
        {
            continue;
        }

        DWORD oldProtect = 0;
        if (VirtualProtect(installed.target, installed.patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            std::memcpy(installed.target, installed.original, installed.patchSize);
            DWORD ignored = 0;
            VirtualProtect(installed.target, installed.patchSize, oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), installed.target, installed.patchSize);
        }

        if (installed.trampoline != nullptr)
        {
            VirtualFree(installed.trampoline, 0, MEM_RELEASE);
        }
    }

    g_inlineHooks.clear();
}

bool IsOciImportName(const char* name)
{
    return _stricmp(name, "oci.dll") == 0;
}

void AddPatch(void** slot, void* original, void* hook)
{
    const auto existing = std::find_if(
        g_patches.begin(),
        g_patches.end(),
        [&](const PatchedImport& patch)
        {
            return patch.slot == slot;
        });

    if (existing == g_patches.end())
    {
        g_patches.push_back(PatchedImport{ slot, original, hook });
    }
}

int PatchModule(HMODULE module)
{
    if (module == nullptr || module == g_module)
    {
        return 0;
    }

    auto* base = reinterpret_cast<std::uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return 0;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        return 0;
    }

    const auto& importDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0)
    {
        return 0;
    }

    int patched = 0;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor)
    {
        const char* importDll = reinterpret_cast<const char*>(base + descriptor->Name);
        if (!IsOciImportName(importDll))
        {
            continue;
        }

        auto* originalThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* firstThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        if (descriptor->OriginalFirstThunk == 0)
        {
            originalThunk = firstThunk;
        }

        for (; originalThunk->u1.AddressOfData != 0; ++originalThunk, ++firstThunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(originalThunk->u1.Ordinal))
            {
                continue;
            }

            auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + originalThunk->u1.AddressOfData);
            void* hook = HookForImportName(reinterpret_cast<const char*>(importByName->Name));
            if (hook == nullptr)
            {
                continue;
            }

            auto** slot = reinterpret_cast<void**>(&firstThunk->u1.Function);
            if (*slot == hook)
            {
                continue;
            }

            DWORD oldProtect = 0;
            if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
            {
                AddPatch(slot, *slot, hook);
                *slot = hook;
                DWORD ignored = 0;
                VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
                ++patched;
            }
        }
    }

    return patched;
}

int PatchLoadedModules()
{
    int patched = 0;
    const DWORD processId = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return patched;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            patched += PatchModule(entry.hModule);
        }
        while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return patched;
}

void RestorePatches()
{
    for (const auto& patch : g_patches)
    {
        if (patch.slot == nullptr || *patch.slot != patch.hook)
        {
            continue;
        }

        DWORD oldProtect = 0;
        if (VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
        {
            *patch.slot = patch.original;
            DWORD ignored = 0;
            VirtualProtect(patch.slot, sizeof(void*), oldProtect, &ignored);
        }
    }
}

bool LoadOciExports()
{
    HMODULE oci = GetModuleHandleW(L"oci.dll");
    if (oci == nullptr)
    {
        oci = GetModuleHandleW(L"OCI.dll");
    }

    if (oci == nullptr)
    {
        SendInfo("oci.dll is not loaded");
        return false;
    }

    g_OCIStmtPrepare = reinterpret_cast<OCIStmtPrepareFn>(GetProcAddress(oci, "OCIStmtPrepare"));
    g_OCIStmtPrepare2 = reinterpret_cast<OCIStmtPrepare2Fn>(GetProcAddress(oci, "OCIStmtPrepare2"));
    g_OCIBindByName = reinterpret_cast<OCIBindByNameFn>(GetProcAddress(oci, "OCIBindByName"));
    g_OCIBindByPos = reinterpret_cast<OCIBindByPosFn>(GetProcAddress(oci, "OCIBindByPos"));
    g_OCIBindByName2 = reinterpret_cast<OCIBindByName2Fn>(GetProcAddress(oci, "OCIBindByName2"));
    g_OCIBindByPos2 = reinterpret_cast<OCIBindByPos2Fn>(GetProcAddress(oci, "OCIBindByPos2"));
    g_OCIStmtExecute = reinterpret_cast<OCIStmtExecuteFn>(GetProcAddress(oci, "OCIStmtExecute"));
    g_OCITransCommit = reinterpret_cast<OCITransCommitFn>(GetProcAddress(oci, "OCITransCommit"));
    g_OCITransRollback = reinterpret_cast<OCITransRollbackFn>(GetProcAddress(oci, "OCITransRollback"));
    g_OCIErrorGet = reinterpret_cast<OCIErrorGetFn>(GetProcAddress(oci, "OCIErrorGet"));

    return g_OCIStmtPrepare != nullptr &&
        g_OCIStmtPrepare2 != nullptr &&
        g_OCIBindByName != nullptr &&
        g_OCIStmtExecute != nullptr &&
        g_OCITransCommit != nullptr &&
        g_OCITransRollback != nullptr &&
        g_OCIErrorGet != nullptr;
}

int InstallOciInlineHooks()
{
    int installed = 0;

    installed += InstallInlineHook(
        reinterpret_cast<void*>(g_OCIStmtPrepare),
        reinterpret_cast<void*>(&HookOCIStmtPrepare),
        reinterpret_cast<void**>(&g_OCIStmtPrepare)) ? 1 : 0;

    installed += InstallInlineHook(
        reinterpret_cast<void*>(g_OCIStmtPrepare2),
        reinterpret_cast<void*>(&HookOCIStmtPrepare2),
        reinterpret_cast<void**>(&g_OCIStmtPrepare2)) ? 1 : 0;

    installed += InstallInlineHook(
        reinterpret_cast<void*>(g_OCIBindByName),
        reinterpret_cast<void*>(&HookOCIBindByName),
        reinterpret_cast<void**>(&g_OCIBindByName)) ? 1 : 0;

    if (g_OCIBindByPos != nullptr)
    {
        installed += InstallInlineHook(
            reinterpret_cast<void*>(g_OCIBindByPos),
            reinterpret_cast<void*>(&HookOCIBindByPos),
            reinterpret_cast<void**>(&g_OCIBindByPos)) ? 1 : 0;
    }

    if (g_OCIBindByName2 != nullptr)
    {
        installed += InstallInlineHook(
            reinterpret_cast<void*>(g_OCIBindByName2),
            reinterpret_cast<void*>(&HookOCIBindByName2),
            reinterpret_cast<void**>(&g_OCIBindByName2)) ? 1 : 0;
    }

    if (g_OCIBindByPos2 != nullptr)
    {
        installed += InstallInlineHook(
            reinterpret_cast<void*>(g_OCIBindByPos2),
            reinterpret_cast<void*>(&HookOCIBindByPos2),
            reinterpret_cast<void**>(&g_OCIBindByPos2)) ? 1 : 0;
    }

    installed += InstallInlineHook(
        reinterpret_cast<void*>(g_OCIStmtExecute),
        reinterpret_cast<void*>(&HookOCIStmtExecute),
        reinterpret_cast<void**>(&g_OCIStmtExecute)) ? 1 : 0;

    installed += InstallInlineHook(
        reinterpret_cast<void*>(g_OCITransCommit),
        reinterpret_cast<void*>(&HookOCITransCommit),
        reinterpret_cast<void**>(&g_OCITransCommit)) ? 1 : 0;

    installed += InstallInlineHook(
        reinterpret_cast<void*>(g_OCITransRollback),
        reinterpret_cast<void*>(&HookOCITransRollback),
        reinterpret_cast<void**>(&g_OCITransRollback)) ? 1 : 0;

    return installed;
}

DWORD WINAPI AgentThread(LPVOID)
{
    ConnectPipe();
    StartPipeWriter();
    SendHello();

    if (!LoadOciExports())
    {
        SendInfo("failed to resolve required OCI exports");
        return 0;
    }

    const int exportHookCount = InstallOciInlineHooks();
    const int importHookCount = PatchLoadedModules();
    SendInfo("hooked OCI exports: " + std::to_string(exportHookCount) + ", imports: " + std::to_string(importHookCount));
    return 0;
}
}

extern "C" sword __cdecl HookOCIStmtPrepare(
    OCIStmt* stmtp,
    OCIError* errhp,
    const OraText* stmt,
    ub4 stmt_len,
    ub4 language,
    ub4 mode)
{
    const sword status = g_OCIStmtPrepare(stmtp, errhp, stmt, stmt_len, language, mode);
    if (stmtp != nullptr)
    {
        const auto sql = BytesToUtf8(stmt, stmt_len);
        ResetStatement(stmtp);
        StoreSql(stmtp, sql);
        SendPrepare(stmtp, sql);
    }

    return status;
}

extern "C" sword __cdecl HookOCIStmtPrepare2(
    OCISvcCtx* svchp,
    OCIStmt** stmtp,
    OCIError* errhp,
    const OraText* stmt,
    ub4 stmt_len,
    const OraText* key,
    ub4 key_len,
    ub4 language,
    ub4 mode)
{
    const sword status = g_OCIStmtPrepare2(svchp, stmtp, errhp, stmt, stmt_len, key, key_len, language, mode);
    if (stmtp != nullptr && *stmtp != nullptr)
    {
        const auto sql = BytesToUtf8(stmt, stmt_len);
        ResetStatement(*stmtp);
        StoreSql(*stmtp, sql);
        SendPrepare(*stmtp, sql);
    }

    return status;
}

extern "C" sword __cdecl HookOCIBindByName(
    OCIStmt* stmtp,
    void** bindpp,
    OCIError* errhp,
    const OraText* placeholder,
    sb4 placeh_len,
    void* valuep,
    sb4 value_sz,
    ub2 dty,
    void* indp,
    ub2* alenp,
    ub2* rcodep,
    ub4 maxarr_len,
    ub4* curelep,
    ub4 mode)
{
    const sword status = g_OCIBindByName(stmtp, bindpp, errhp, placeholder, placeh_len, valuep, value_sz, dty, indp, alenp, rcodep, maxarr_len, curelep, mode);
    if (stmtp != nullptr)
    {
        BindCapture bind{};
        bind.index = 0;
        bind.name = StripLeadingColon(OptionalOraTextToUtf8(placeholder, placeh_len));
        bind.value = valuep;
        bind.valueSize = value_sz;
        bind.dataType = dty;
        bind.error = errhp;
        bind.actualLength = alenp;
        StoreBind(stmtp, bind);
    }

    return status;
}

extern "C" sword __cdecl HookOCIBindByPos(
    OCIStmt* stmtp,
    void** bindpp,
    OCIError* errhp,
    ub4 position,
    void* valuep,
    sb4 value_sz,
    ub2 dty,
    void* indp,
    ub2* alenp,
    ub2* rcodep,
    ub4 maxarr_len,
    ub4* curelep,
    ub4 mode)
{
    const sword status = g_OCIBindByPos != nullptr
        ? g_OCIBindByPos(stmtp, bindpp, errhp, position, valuep, value_sz, dty, indp, alenp, rcodep, maxarr_len, curelep, mode)
        : 0;

    if (stmtp != nullptr)
    {
        BindCapture bind{};
        bind.index = static_cast<int>(position);
        bind.value = valuep;
        bind.valueSize = value_sz;
        bind.dataType = dty;
        bind.error = errhp;
        bind.actualLength = alenp;
        StoreBind(stmtp, bind);
    }

    return status;
}

extern "C" sword __cdecl HookOCIBindByName2(
    OCIStmt* stmtp,
    void** bindpp,
    OCIError* errhp,
    const OraText* placeholder,
    sb4 placeh_len,
    void* valuep,
    long long value_sz,
    ub2 dty,
    void* indp,
    ub4* alenp,
    ub2* rcodep,
    ub4 maxarr_len,
    ub4* curelep,
    ub4 mode)
{
    const sword status = g_OCIBindByName2 != nullptr
        ? g_OCIBindByName2(stmtp, bindpp, errhp, placeholder, placeh_len, valuep, value_sz, dty, indp, alenp, rcodep, maxarr_len, curelep, mode)
        : 0;

    if (stmtp != nullptr)
    {
        BindCapture bind{};
        bind.index = 0;
        bind.name = StripLeadingColon(OptionalOraTextToUtf8(placeholder, placeh_len));
        bind.value = valuep;
        bind.valueSize = value_sz > INT_MAX ? INT_MAX : static_cast<sb4>(value_sz);
        bind.dataType = dty;
        bind.error = errhp;
        bind.actualLength32 = alenp;
        StoreBind(stmtp, bind);
    }

    return status;
}

extern "C" sword __cdecl HookOCIBindByPos2(
    OCIStmt* stmtp,
    void** bindpp,
    OCIError* errhp,
    ub4 position,
    void* valuep,
    long long value_sz,
    ub2 dty,
    void* indp,
    ub4* alenp,
    ub2* rcodep,
    ub4 maxarr_len,
    ub4* curelep,
    ub4 mode)
{
    const sword status = g_OCIBindByPos2 != nullptr
        ? g_OCIBindByPos2(stmtp, bindpp, errhp, position, valuep, value_sz, dty, indp, alenp, rcodep, maxarr_len, curelep, mode)
        : 0;

    if (stmtp != nullptr)
    {
        BindCapture bind{};
        bind.index = static_cast<int>(position);
        bind.value = valuep;
        bind.valueSize = value_sz > INT_MAX ? INT_MAX : static_cast<sb4>(value_sz);
        bind.dataType = dty;
        bind.error = errhp;
        bind.actualLength32 = alenp;
        StoreBind(stmtp, bind);
    }

    return status;
}

extern "C" sword __cdecl HookOCIStmtExecute(
    OCISvcCtx* svchp,
    OCIStmt* stmtp,
    OCIError* errhp,
    ub4 iters,
    ub4 rowoff,
    const void* snap_in,
    void* snap_out,
    ub4 mode)
{
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&start);
    const sword status = g_OCIStmtExecute(svchp, stmtp, errhp, iters, rowoff, snap_in, snap_out, mode);
    QueryPerformanceCounter(&end);

    if (stmtp != nullptr)
    {
        sb4 errorCode = 0;
        const auto errorMessage = CaptureOciError(errhp, status, &errorCode);
        const auto snapshot = SnapshotStatement(stmtp);
        const auto signature = BuildExecuteSignature(snapshot.sql, snapshot.binds, status, errorMessage);
        if (ShouldSuppressExecute(stmtp, signature, GetTickCount64()))
        {
            return status;
        }

        for (const auto& bind : snapshot.binds)
        {
            SendBind(stmtp, bind);
        }

        SendExecuteCompleted(stmtp, ElapsedMilliseconds(start, end), status, errorCode, errorMessage);
    }

    return status;
}

extern "C" sword __cdecl HookOCITransCommit(OCISvcCtx* svchp, OCIError* errhp, ub4 flags)
{
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&start);
    const sword status = g_OCITransCommit(svchp, errhp, flags);
    QueryPerformanceCounter(&end);
    SendTransactionCompleted("commit_completed", ElapsedMilliseconds(start, end), status);
    return status;
}

extern "C" sword __cdecl HookOCITransRollback(OCISvcCtx* svchp, OCIError* errhp, ub4 flags)
{
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&start);
    const sword status = g_OCITransRollback(svchp, errhp, flags);
    QueryPerformanceCounter(&end);
    SendTransactionCompleted("rollback_completed", ElapsedMilliseconds(start, end), status);
    return status;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_pipeLock);
        InitializeCriticalSection(&g_queueLock);
        InitializeCriticalSection(&g_stateLock);
        InitializeConditionVariable(&g_queueAvailable);
        HANDLE thread = CreateThread(nullptr, 0, AgentThread, nullptr, 0, nullptr);
        if (thread != nullptr)
        {
            CloseHandle(thread);
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        InterlockedExchange(&g_unloading, 1);
        WakeAllConditionVariable(&g_queueAvailable);
        RestorePatches();
        RestoreInlineHooks();
        ClosePipe();
        StopPipeWriter();
        DeleteCriticalSection(&g_stateLock);
        DeleteCriticalSection(&g_queueLock);
        DeleteCriticalSection(&g_pipeLock);
        break;
    default:
        break;
    }

    return TRUE;
}
