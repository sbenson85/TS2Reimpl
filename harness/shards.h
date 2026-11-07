#pragma once
// --jobs N: runs the selected tests in N runner processes at once ("shards") and prints their output as one run's.
//
// The selected tests are split into N contiguous ranges of the registry order, balanced on each test's duration from
// the last runs (the times file; a test without a recorded time weighs the mean of those that have one, or 1 when none
// has), so a shard runs its tests in the order a single run would, and each test has the same predecessors as there
// except the first of each shard. The plan depends only on the selection and the times file, and is printed first.
// Each shard is this exe again with `--filter @<file of its test names>` (and its own monitor, harness/monitor.h), at
// below-normal priority, in a job object that dies with this process (a caller's timeout kills every shard).
// Output: the shards' stdout in shard order (so in registry order), streamed as the shard at the head finishes tests,
// without the shards' own totals lines, then one totals line "N run, M failed" for the whole run; their stderr after
// that, in shard order. A shard whose runner dies keeps its "DIED ..." report and is started again on the tests after
// the one it died in ("difftest: shard ... restarted" line); a died test counts as run and failed. Exit code: the first
// (in shard order) death's exit code, else 1 when a test failed, else 0.
// --json: the shards' JSON merged (a died runner writes none: its tests come from its output lines).
// Each test's time (from its result line's arrival) goes back to the times file (--times <file>, default
// difftest.times.txt next to this exe), replaced atomically.
#include "difftest.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace shards {

using Clock = std::chrono::steady_clock;

struct Line {
    std::string text;
    double seconds; // since the shard's start
};

// One run of a difftest.exe over a list of tests (a shard's first, or a restart after a death).
struct Attempt {
    std::vector<std::string> names;
    std::wstring listFile, jsonFile;
    size_t firstLine = 0;  // index in Shard::lines of this attempt's first output line
    Clock::time_point start;
    DWORD exitCode = 0;
    bool completed = false; // printed its totals line
    int run = 0, failed = 0; // from its totals line, or counted from its result lines when it died
    std::string diedIn;     // the test the monitor says it died in
    std::string stderrText;
};

struct Shard {
    std::vector<std::string> names;
    std::vector<Line> lines; // what is printed for this shard (its runners' lines, minus totals and notes)
    std::vector<Attempt> attempts;
    bool done = false;
    double seconds = 0;
};

struct Run {
    std::wstring self, imagePath, jsonPath, tempDir;
    std::vector<Shard> shards;
    std::mutex mutex;
    std::condition_variable changed;
    HANDLE job = nullptr;
    Clock::time_point start;
};

inline std::wstring Widen(const std::string &s) { return std::wstring(s.begin(), s.end()); }
inline std::string Narrow(const std::wstring &s)
{
    std::string out;
    for (wchar_t c : s) out += static_cast<char>(c);
    return out;
}

inline bool IsTotals(const std::string &l)
{
    int run = 0, failed = 0;
    char rest = 0;
    return std::sscanf(l.c_str(), "%d run, %d failed%c", &run, &failed, &rest) == 2;
}

// "[pass] Name (rva 0x..., N cases)": the status and the name, or false.
inline bool ParseResult(const std::string &l, std::string &status, std::string &name)
{
    if (l.size() < 3 || l[0] != '[') return false;
    const size_t close = l.find("] ");
    if (close == std::string::npos) return false;
    status = l.substr(1, close - 1);
    if (status != "pass" && status != "fail" && status != "error") return false;
    const size_t end = l.find(" (rva ", close + 2);
    if (end == std::string::npos) return false;
    name = l.substr(close + 2, end - close - 2);
    return true;
}

inline std::string JsonEscape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

inline std::map<std::string, double> ReadTimes(const std::wstring &path)
{
    std::map<std::string, double> times;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        times[line.substr(tab + 1)] = std::atof(line.substr(0, tab).c_str());
    }
    return times;
}

inline void WriteTimes(const std::wstring &path, const std::map<std::string, double> &times)
{
    const std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        for (auto &[name, ms] : times) out << static_cast<long long>(ms + 0.5) << '\t' << name << '\n';
        if (!out) return;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(temp.c_str());
}

// Contiguous ranges of `weights` (in order), `count` of them, each as close to an equal share as the boundaries allow.
// Returns each range's first index (the first is 0); count <= weights.size().
inline std::vector<size_t> Partition(const std::vector<double> &weights, size_t count)
{
    const size_t n = weights.size();
    std::vector<double> prefix(n + 1, 0.0); // prefix[b]: the weight before index b
    for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + weights[i];
    std::vector<size_t> starts{0};
    for (size_t j = 1; j < count; ++j) {
        const double target = prefix[n] * static_cast<double>(j) / static_cast<double>(count);
        const size_t lo = starts.back() + 1, hi = n - (count - j); // every range keeps at least one test
        size_t b = lo;
        while (b < hi && prefix[b] < target) ++b;
        if (b > lo && target - prefix[b - 1] < prefix[b] - target) --b; // the boundary before is closer
        starts.push_back(b);
    }
    return starts;
}

inline bool Launch(Run &r, Attempt &a, HANDLE &process, HANDLE &out, HANDLE &err)
{
    {
        std::ofstream list(a.listFile, std::ios::trunc);
        for (const std::string &n : a.names) list << n << '\n';
        if (!list) return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outWrite = nullptr, errWrite = nullptr;
    if (!CreatePipe(&out, &outWrite, &sa, 1 << 16)) return false;
    if (!CreatePipe(&err, &errWrite, &sa, 1 << 16)) return false;
    SetHandleInformation(out, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err, HANDLE_FLAG_INHERIT, 0);
    // Only this shard's pipe ends are inherited: a shard holding another's write end would keep that pipe open.
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attributes(size);
    auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    HANDLE inherit[2] = {outWrite, errWrite};
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size) ||
        !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr))
        return false;
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = outWrite;
    si.StartupInfo.hStdError = errWrite;
    si.lpAttributeList = list;
    std::wstring cmd = L"\"" + r.self + L"\" --image \"" + r.imagePath + L"\" --filter \"@" + a.listFile + L"\"";
    if (!a.jsonFile.empty()) cmd += L" --json \"" + a.jsonFile + L"\"";
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(r.self.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr,
                                   &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(list);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!ok) return false;
    if (r.job) AssignProcessToJobObject(r.job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    return true;
}

// Runs one shard to its end: its tests, then again on the rest after each death that got past at least one test.
inline void Supervise(Run &r, size_t index)
{
    Shard &shard = r.shards[index];
    std::vector<std::string> todo = shard.names;
    const Clock::time_point shardStart = Clock::now();
    auto add = [&](const std::string &text) {
        std::lock_guard<std::mutex> lock(r.mutex);
        shard.lines.push_back({text, std::chrono::duration<double>(Clock::now() - r.start).count()});
        r.changed.notify_all();
    };
    for (int attempt = 0; !todo.empty(); ++attempt) {
        Attempt a;
        a.names = todo;
        const std::wstring stem = r.tempDir + L"shard-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(index) + L"-" +
                                  std::to_wstring(attempt);
        a.listFile = stem + L".txt";
        if (!r.jsonPath.empty()) a.jsonFile = stem + L".json";
        a.start = Clock::now();
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            a.firstLine = shard.lines.size();
        }
        HANDLE process = nullptr, out = nullptr, err = nullptr;
        if (!Launch(r, a, process, out, err)) {
            a.exitCode = 2;
            add("difftest: shard " + std::to_string(index + 1) + " could not start a runner (error " + std::to_string(GetLastError()) + ")");
            std::lock_guard<std::mutex> lock(r.mutex);
            shard.attempts.push_back(std::move(a));
            break;
        }
        std::thread errReader([&] {
            char buf[4096];
            DWORD got = 0;
            while (ReadFile(err, buf, sizeof(buf), &got, nullptr) && got) a.stderrText.append(buf, got);
        });
        std::string pending, status, name, lastFinished;
        auto take = [&](std::string text) {
            if (!text.empty() && text.back() == '\r') text.pop_back();
            if (IsTotals(text)) {
                std::sscanf(text.c_str(), "%d run, %d failed", &a.run, &a.failed);
                a.completed = true;
                return;
            }
            if (ParseResult(text, status, name)) {
                lastFinished = name;
                if (!a.completed) {
                    ++a.run; // recounted from the totals line when there is one
                    a.failed += status != "pass";
                }
            }
            if (text.rfind("DIED in test ", 0) == 0) {
                const size_t end = text.find(" (", 13);
                a.diedIn = text.substr(13, end == std::string::npos ? std::string::npos : end - 13);
            }
            add(text);
        };
        char buf[4096];
        DWORD got = 0;
        while (ReadFile(out, buf, sizeof(buf), &got, nullptr) && got) {
            pending.append(buf, got);
            for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
                take(pending.substr(0, nl));
                pending.erase(0, nl + 1);
            }
        }
        if (!pending.empty()) take(pending);
        WaitForSingleObject(process, INFINITE);
        GetExitCodeProcess(process, &a.exitCode);
        CloseHandle(process);
        errReader.join();
        CloseHandle(out);
        CloseHandle(err);
        DeleteFileW(a.listFile.c_str());
        todo.clear();
        if (!a.completed) {
            if (!a.diedIn.empty()) {
                ++a.run;
                ++a.failed;
            }
            // Go on after the test it died in, or after the last one that finished (died between tests); without
            // either, nothing ran and a restart would die the same way.
            const std::string &after = !a.diedIn.empty() ? a.diedIn : lastFinished;
            auto it = after.empty() ? a.names.end() : std::find(a.names.begin(), a.names.end(), after);
            if (it != a.names.end()) todo.assign(it + 1, a.names.end());
            char code[16];
            std::snprintf(code, sizeof(code), "0x%08lx", a.exitCode);
            if (!todo.empty())
                add("difftest: shard " + std::to_string(index + 1) + " restarted after its runner died (exit code " + code + "): " +
                    std::to_string(todo.size()) + " tests left, from " + todo.front());
            else if (after.empty())
                add("difftest: shard " + std::to_string(index + 1) + "'s runner ended without running a test (exit code " + code +
                    "; its stderr follows the totals)");
        }
        std::lock_guard<std::mutex> lock(r.mutex);
        shard.attempts.push_back(std::move(a));
    }
    std::lock_guard<std::mutex> lock(r.mutex);
    shard.seconds = std::chrono::duration<double>(Clock::now() - shardStart).count();
    shard.done = true;
    r.changed.notify_all();
}

// The JSON entries of a died attempt, from its output lines.
inline std::string EntriesFromLines(const Shard &shard, size_t from, size_t to, const Attempt &a)
{
    std::string json;
    bool open = false;
    auto close = [&] {
        if (open) json += "]}";
        open = false;
    };
    for (size_t i = from; i < to; ++i) {
        const std::string &l = shard.lines[i].text;
        std::string status, name;
        if (ParseResult(l, status, name)) {
            close();
            unsigned rva = 0;
            unsigned long long cases = 0;
            std::sscanf(l.c_str() + l.find(" (rva ") + 6, "0x%x, %llu", &rva, &cases);
            char head[64];
            std::snprintf(head, sizeof(head), "\",\"rva\":\"0x%x\",\"status\":\"", rva);
            json += std::string(json.empty() ? "" : ",") + "{\"name\":\"" + JsonEscape(name) + head + status + "\",\"cases\":" +
                    std::to_string(cases) + ",\"failures\":[";
            open = true;
            bool first = true;
            for (size_t k = i + 1; k < to && shard.lines[k].text.rfind("    ", 0) == 0; ++k) {
                json += std::string(first ? "\"" : ",\"") + JsonEscape(shard.lines[k].text.substr(4)) + "\"";
                first = false;
            }
        } else if (l.rfind("DIED in test ", 0) == 0) {
            close();
            json += std::string(json.empty() ? "" : ",") + "{\"name\":\"" + JsonEscape(a.diedIn) + "\",\"status\":\"died\",\"failures\":[\"" +
                    JsonEscape(l) + "\"]}";
        }
    }
    close();
    return json;
}

inline int RunSharded(const std::vector<const difftest::Test *> &selected, const std::wstring &imagePath, const std::wstring &jsonPath,
                      const std::wstring &timesPath, int jobs)
{
    Run r;
    r.start = Clock::now();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    r.self = self;
    r.imagePath = imagePath;
    r.jsonPath = jsonPath;
    std::wstring times = timesPath;
    if (times.empty()) times = r.self.substr(0, r.self.find_last_of(L'\\') + 1) + L"difftest.times.txt";
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    r.tempDir = std::wstring(temp) + L"ts2difftest\\";
    CreateDirectoryW(r.tempDir.c_str(), nullptr);

    // The test names in registry order, each once (a name registered twice runs both times in the shard naming it).
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const difftest::Test *t : selected)
        if (seen.insert(t->name).second) names.push_back(t->name);
    std::map<std::string, double> known = ReadTimes(times);
    double knownSum = 0;
    size_t knownCount = 0;
    for (const std::string &n : names)
        if (auto it = known.find(n); it != known.end()) {
            knownSum += it->second;
            ++knownCount;
        }
    const double fallback = knownCount ? (std::max)(1.0, knownSum / static_cast<double>(knownCount)) : 1.0;
    std::vector<double> weights;
    for (const std::string &n : names) {
        auto it = known.find(n);
        weights.push_back(it != known.end() ? (std::max)(1.0, it->second) : fallback);
    }
    const size_t count = (std::min)(names.size(), static_cast<size_t>(jobs));
    const std::vector<size_t> starts = Partition(weights, count);
    std::string plan;
    r.shards.resize(starts.size());
    for (size_t s = 0; s < starts.size(); ++s) {
        const size_t end = s + 1 < starts.size() ? starts[s + 1] : names.size();
        r.shards[s].names.assign(names.begin() + static_cast<std::ptrdiff_t>(starts[s]), names.begin() + static_cast<std::ptrdiff_t>(end));
        double ms = 0;
        for (size_t i = starts[s]; i < end; ++i) ms += weights[i];
        char b[96];
        std::snprintf(b, sizeof(b), "%s%zu-%zu", s ? ", " : "", starts[s] + 1, end);
        plan += b;
        if (knownCount) {
            std::snprintf(b, sizeof(b), " (~%.0f s)", ms / 1000.0);
            plan += b;
        }
    }
    std::printf("difftest: %zu shards (--jobs %d) over %zu tests in registry order; %zu with a recorded time in %s: tests %s\n",
                r.shards.size(), jobs, names.size(), knownCount, Narrow(times).c_str(), plan.c_str());
    std::fflush(stdout);

    // Every runner dies with this process (a caller's timeout).
    r.job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (r.job) SetInformationJobObject(r.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    std::vector<std::thread> supervisors;
    for (size_t s = 0; s < r.shards.size(); ++s) supervisors.emplace_back(Supervise, std::ref(r), s);

    // Print the head shard's lines as they come, then the next shard's once the head is done.
    {
        std::unique_lock<std::mutex> lock(r.mutex);
        for (size_t head = 0, printed = 0; head < r.shards.size();) {
            r.changed.wait(lock, [&] { return r.shards[head].lines.size() > printed || r.shards[head].done; });
            Shard &shard = r.shards[head];
            std::vector<std::string> batch;
            for (; printed < shard.lines.size(); ++printed) batch.push_back(shard.lines[printed].text);
            const bool finished = shard.done;
            lock.unlock();
            for (const std::string &l : batch) std::printf("%s\n", l.c_str());
            std::fflush(stdout);
            lock.lock();
            if (finished && printed == shard.lines.size()) {
                ++head;
                printed = 0;
            }
        }
    }
    for (std::thread &t : supervisors) t.join();

    int run = 0, failed = 0;
    DWORD exitCode = 0;
    bool died = false;
    std::string json;
    for (Shard &shard : r.shards) {
        for (size_t k = 0; k < shard.attempts.size(); ++k) {
            Attempt &a = shard.attempts[k];
            run += a.run;
            failed += a.failed;
            if (!a.completed && !died) {
                died = true;
                exitCode = a.exitCode ? a.exitCode : 1;
            }
            if (!r.jsonPath.empty()) {
                std::string entries;
                if (a.completed) {
                    std::ifstream in(a.jsonFile, std::ios::binary);
                    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    const size_t open = text.find('['), close = text.rfind("],\"run\":");
                    if (open != std::string::npos && close != std::string::npos && close > open) entries = text.substr(open + 1, close - open - 1);
                } else {
                    const size_t to = k + 1 < shard.attempts.size() ? shard.attempts[k + 1].firstLine : shard.lines.size();
                    entries = EntriesFromLines(shard, a.firstLine, to, a);
                }
                if (!entries.empty()) json += (json.empty() ? "" : ",") + entries;
            }
            if (!a.jsonFile.empty()) DeleteFileW(a.jsonFile.c_str());
        }
    }

    // Each test's time: from the previous result line (or the runner's start) to its own.
    for (Shard &shard : r.shards) {
        for (size_t k = 0; k < shard.attempts.size(); ++k) {
            const Attempt &a = shard.attempts[k];
            const size_t to = k + 1 < shard.attempts.size() ? shard.attempts[k + 1].firstLine : shard.lines.size();
            double previous = std::chrono::duration<double>(a.start - r.start).count();
            bool first = true; // its time includes the runner's start-up: kept only when the test has no other
            for (size_t i = a.firstLine; i < to; ++i) {
                std::string status, name;
                if (!ParseResult(shard.lines[i].text, status, name)) continue;
                if (!first || !known.count(name)) known[name] = (std::max)(0.0, (shard.lines[i].seconds - previous) * 1000.0);
                previous = shard.lines[i].seconds;
                first = false;
            }
        }
    }
    WriteTimes(times, known);

    std::string took;
    for (size_t s = 0; s < r.shards.size(); ++s) {
        char b[32];
        std::snprintf(b, sizeof(b), "%s%.0f", s ? ", " : "", r.shards[s].seconds);
        took += b;
    }
    std::printf("difftest: shards took %s s (wall %.0f s)\n", took.c_str(), std::chrono::duration<double>(Clock::now() - r.start).count());
    std::printf("%d run, %d failed\n", run, failed);
    std::fflush(stdout);
    for (Shard &shard : r.shards)
        for (Attempt &a : shard.attempts) {
            // The runners wrote CRLF; stderr is in text mode here too, so pass LF (or each line would end in CR CR LF).
            std::string text;
            for (size_t i = 0; i < a.stderrText.size(); ++i)
                if (!(a.stderrText[i] == '\r' && i + 1 < a.stderrText.size() && a.stderrText[i + 1] == '\n')) text += a.stderrText[i];
            std::fwrite(text.data(), 1, text.size(), stderr);
        }
    std::fflush(stderr);
    if (!r.jsonPath.empty())
        std::ofstream(r.jsonPath) << "{\"tests\":[" << json << "],\"run\":" << run << ",\"failed\":" << failed << "}\n";
    if (r.job) CloseHandle(r.job);
    if (died) return static_cast<int>(exitCode);
    return failed ? 1 : 0;
}

} // namespace shards
