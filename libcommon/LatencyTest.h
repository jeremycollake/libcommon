#pragma once

// LatencyTest: ISR and DPC execution times per driver and per CPU, timer wake latency and (optionally) hard page faults
// per process, from a real-time kernel ETW session (plans/dpc-latency-monitor.md in Process Lasso). No kernel driver.
//
// Session: Windows 8+ starts a private system logger (EVENT_TRACE_SYSTEM_LOGGER_MODE) named SessionName, so it never
// collides with WPR, xperf or other tools that use "NT Kernel Logger". Windows 7 has to use "NT Kernel Logger"; a
// volatile registry marker records that we own it, so a session left by a crashed Process Lasso can be stopped while
// one owned by another tool is left alone.
//
// Execution time = event timestamp - InitialTime (both raw QPC: ClientContext 1 plus PROCESS_TRACE_MODE_RAW_TIMESTAMP).
// Routine addresses map to kernel modules from SystemModuleInformation. Framework drivers (Wdf01000.sys, ndis.sys,
// storport.sys, dxgkrnl.sys, ...) run callbacks for many client drivers, so their entries cover several drivers.
//
// Thread wake latency: a time-critical thread waits on a 1 ms timer; for each wake, the latency is the time from the
// kernel readying the thread (its ReadyThread event) to the thread running (QPC right after the wait returns). That leaves
// out timer granularity, which a due-time comparison would measure instead, and needs no change to the system timer.
//
// Nothing runs until Start(); Stop() ends the session and threads and keeps the results, so Start() resumes.

#include <windows.h>
// Process Lasso's NtApi.h includes the DDK ntddk.h (inside namespace NT), which leaves _NTDDK_ defined, and with it
// evntrace.h declares only its kernel-mode subset (no sessions, no consumers)
#pragma push_macro("_NTDDK_")
#undef _NTDDK_
#include <evntrace.h>
#include <evntcons.h>
#pragma pop_macro("_NTDDK_")
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <set>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "version.lib")

#ifndef EVENT_TRACE_SYSTEM_LOGGER_MODE
#define EVENT_TRACE_SYSTEM_LOGGER_MODE 0x02000000
#endif
#ifndef PROCESS_TRACE_MODE_RAW_TIMESTAMP
#define PROCESS_TRACE_MODE_RAW_TIMESTAMP 0x00001000
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace LatencyTest
{
	constexpr WCHAR SessionName[] = L"Process Lasso LatencyTest";
	constexpr WCHAR KernelLoggerOwnerValueName[] = L"LatencyTestOwnsKernelLogger";

	// verdict thresholds, in microseconds
	constexpr ULONGLONG WarningExecutionUs = 500;
	constexpr ULONGLONG BadExecutionUs = 2000;
	constexpr ULONGLONG WarningWakeUs = 1000;
	constexpr ULONGLONG BadWakeUs = 3000;

	constexpr size_t HistoryLengthSeconds = 300;
	constexpr ULONGLONG MatchDelaySeconds = 3;		// how far thread wake results lag behind (ETW delivers events late)	// per-second buckets kept for the graph
	constexpr int HistogramBuckets = 18;			// [0] < 1 us, [n] = 2^(n-1) .. 2^n - 1 us, last is everything above

	// kernel event classes (classic MOF providers of the SystemTraceProvider)
	constexpr GUID PerfInfoGuid = { 0xce1dbfb4, 0x137e, 0x4da6, { 0x87, 0xb0, 0x3f, 0x59, 0xaa, 0x10, 0x2c, 0xbc } };
	constexpr GUID ThreadGuid = { 0x3d6fa8d1, 0xfe05, 0x11d0, { 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c } };
	constexpr GUID PageFaultGuid = { 0x3d6fa8d3, 0xfe05, 0x11d0, { 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c } };
	constexpr GUID SystemTraceGuid = { 0x9e814aad, 0x3204, 0x11d2, { 0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39 } };
	constexpr GUID PrivateSessionGuid = { 0x6a3c1f52, 0x8d0e, 0x4b8f, { 0x9c, 0x21, 0x5e, 0x4f, 0x77, 0x0b, 0x2d, 0x93 } };

	// PerfInfo opcodes
	constexpr UCHAR OpcodeThreadedDpc = 66;
	constexpr UCHAR OpcodeIsr = 67;
	constexpr UCHAR OpcodeDpc = 68;
	constexpr UCHAR OpcodeTimerDpc = 69;
	constexpr UCHAR OpcodeIsrMsi = 50;
	// PageFault opcode
	constexpr UCHAR OpcodeHardFault = 32;
	// Thread opcode
	constexpr UCHAR OpcodeReadyThread = 50;

	enum Verdict { VerdictUnknown, VerdictGood, VerdictWarning, VerdictBad };

	enum StartResult
	{
		StartOK,
		StartAccessDenied,		// not elevated
		StartKernelLoggerInUse,	// Windows 7: another tool owns "NT Kernel Logger"
		StartNoResources,		// Windows 8+: all system logger sessions are in use
		StartFailed,
	};

	struct ExecutionStats
	{
		ULONGLONG nCount = 0;
		ULONGLONG nTotalTicks = 0;
		ULONGLONG nMaxTicks = 0;
		ULONGLONG nOverWarning = 0;		// executions of WarningExecutionUs or longer
		ULONGLONG nOverBad = 0;			// executions of BadExecutionUs or longer
		ULONGLONG histogram[HistogramBuckets] = {};

		void Add(const ULONGLONG nTicks, const ULONGLONG nTicksPerUs, const ULONGLONG nWarningTicks, const ULONGLONG nBadTicks)
		{
			nCount++;
			nTotalTicks += nTicks;
			if (nTicks > nMaxTicks)
			{
				nMaxTicks = nTicks;
			}
			if (nTicks >= nWarningTicks)
			{
				nOverWarning++;
				if (nTicks >= nBadTicks)
				{
					nOverBad++;
				}
			}
			histogram[GetHistogramBucket(nTicksPerUs ? nTicks / nTicksPerUs : 0)]++;
		}

		void Merge(const ExecutionStats& o)
		{
			nCount += o.nCount;
			nTotalTicks += o.nTotalTicks;
			nMaxTicks = (std::max)(nMaxTicks, o.nMaxTicks);
			nOverWarning += o.nOverWarning;
			nOverBad += o.nOverBad;
			for (int n = 0; n < HistogramBuckets; n++)
			{
				histogram[n] += o.histogram[n];
			}
		}

		static int GetHistogramBucket(ULONGLONG nUs)
		{
			int nBucket = 0;
			while (nUs && nBucket < HistogramBuckets - 1)
			{
				nUs >>= 1;
				nBucket++;
			}
			return nBucket;
		}
	};

	struct ModuleStats
	{
		std::wstring strPath;	// Win32 path when it could be resolved, else the kernel's path
		std::wstring strName;	// file name, e.g. ndis.sys
		ULONGLONG nBase = 0;
		ULONGLONG nSize = 0;
		ExecutionStats isr;
		ExecutionStats dpc;
	};

	struct CPUStats
	{
		ExecutionStats isr;
		ExecutionStats dpc;
		ExecutionStats wake;	// thread wakes that ran on this CPU
	};

	struct ProcessFaults
	{
		DWORD dwProcessId = 0;
		std::wstring strName;
		ULONGLONG nHardFaults = 0;
	};

	struct SecondBucket
	{
		ULONGLONG nMaxIsrTicks = 0;
		ULONGLONG nMaxDpcTicks = 0;
		ULONGLONG nMaxWakeTicks = 0;
		ULONGLONG nIsrTicks = 0;
		ULONGLONG nDpcTicks = 0;
		ULONG nIsrCount = 0;
		ULONG nDpcCount = 0;
	};

	struct Snapshot
	{
		ULONGLONG nFrequency = 1;		// QPC ticks per second
		ULONGLONG nElapsedTicks = 0;	// time spent running (paused time excluded)
		bool bRunning = false;
		bool bHardFaults = false;
		bool bHighResolutionTimer = false;	// false: the wake thread samples once per system timer tick (before Windows 10 1803)
		bool bKernelLogger = false;			// Windows 7 path
		ULONGLONG nEventsLost = 0;
		ULONGLONG nBuffersLost = 0;
		ULONGLONG nUnattributed = 0;		// ISRs/DPCs whose routine was in no known module (also counted in "Unknown")
		ExecutionStats isr;
		ExecutionStats dpc;
		ExecutionStats wake;
		ULONGLONG nHardFaults = 0;
		std::vector<ModuleStats> vModules;		// only modules with ISRs or DPCs
		std::vector<CPUStats> vCPUs;			// index = processor index
		std::vector<ProcessFaults> vProcesses;	// only with hard faults
		std::vector<SecondBucket> vHistory;		// oldest first, the last entry is the current second
		ULONGLONG perfInfoOpcodes[256] = {};	// diagnostics: PerfInfo events seen, by opcode

		double ToMicroseconds(const ULONGLONG nTicks) const
		{
			return nFrequency ? static_cast<double>(nTicks) * 1000000.0 / static_cast<double>(nFrequency) : 0.0;
		}

		double ToMilliseconds(const ULONGLONG nTicks) const
		{
			return ToMicroseconds(nTicks) / 1000.0;
		}

		// percent of all CPU time spent in ISRs or DPCs over the elapsed time
		double GetPercentOfCPU(const ULONGLONG nTicks) const
		{
			const double dTotal = static_cast<double>(nElapsedTicks) * static_cast<double>(vCPUs.empty() ? 1 : vCPUs.size());
			return dTotal > 0 ? static_cast<double>(nTicks) * 100.0 / dTotal : 0.0;
		}

		Verdict GetVerdict() const
		{
			if (isr.nCount + dpc.nCount == 0 && wake.nCount < 100)
			{
				return VerdictUnknown;
			}
			const double dExec = ToMicroseconds((std::max)(isr.nMaxTicks, dpc.nMaxTicks));
			const double dWake = ToMicroseconds(wake.nMaxTicks);
			if (dExec >= BadExecutionUs || dWake >= BadWakeUs)
			{
				return VerdictBad;
			}
			if (dExec >= WarningExecutionUs || dWake >= WarningWakeUs)
			{
				return VerdictWarning;
			}
			return VerdictGood;
		}

		// the module with the longest single ISR or DPC; nullptr if none
		const ModuleStats* GetWorstModule(bool* pbIsr = nullptr) const
		{
			const ModuleStats* pWorst = nullptr;
			ULONGLONG nWorst = 0;
			for (const auto& module : vModules)
			{
				if (module.isr.nMaxTicks > nWorst || module.dpc.nMaxTicks > nWorst)
				{
					pWorst = &module;
					const bool bIsr = module.isr.nMaxTicks >= module.dpc.nMaxTicks;
					nWorst = bIsr ? module.isr.nMaxTicks : module.dpc.nMaxTicks;
					if (pbIsr)
					{
						*pbIsr = bIsr;
					}
				}
			}
			return pWorst;
		}
	};

	//
	// helpers
	//

	inline bool IsWindows8OrLater()
	{
		OSVERSIONINFOEXW osvi = { sizeof(osvi) };
		osvi.dwMajorVersion = 6;
		osvi.dwMinorVersion = 2;
		DWORDLONG nMask = 0;
		VER_SET_CONDITION(nMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
		VER_SET_CONDITION(nMask, VER_MINORVERSION, VER_GREATER_EQUAL);
		return FALSE != VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION, nMask);
	}

	// a processor's system-wide index (the ETW ProcessorIndex), from its group and number
	inline USHORT GetSystemProcessorIndex(const PROCESSOR_NUMBER& processor)
	{
		USHORT nIndex = processor.Number;
		for (WORD nGroup = 0; nGroup < processor.Group; nGroup++)
		{
			nIndex = static_cast<USHORT>(nIndex + GetActiveProcessorCount(nGroup));
		}
		return nIndex;
	}

	inline bool EnablePrivilege(const WCHAR* pwszPrivilege)
	{
		HANDLE hToken = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		{
			return false;
		}
		TOKEN_PRIVILEGES tp = {};
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		bool bResult = LookupPrivilegeValueW(nullptr, pwszPrivilege, &tp.Privileges[0].Luid)
			&& AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr) && ERROR_SUCCESS == GetLastError();
		CloseHandle(hToken);
		return bResult;
	}

	// \SystemRoot\..., \??\C:\..., \Windows\... to a Win32 path
	inline std::wstring KernelPathToWin32(const std::wstring& strPath)
	{
		WCHAR wszWindows[MAX_PATH] = {};
		GetWindowsDirectoryW(wszWindows, _countof(wszWindows));
		auto starts = [&](const WCHAR* pwszPrefix) { return 0 == _wcsnicmp(strPath.c_str(), pwszPrefix, wcslen(pwszPrefix)); };
		if (starts(L"\\SystemRoot\\"))
		{
			return std::wstring(wszWindows) + strPath.substr(11);
		}
		if (starts(L"\\??\\"))
		{
			return strPath.substr(4);
		}
		if (starts(L"\\Windows\\"))
		{
			return std::wstring(wszWindows) + strPath.substr(8);
		}
		if (strPath.find(L'\\') == std::wstring::npos)
		{
			return std::wstring(wszWindows) + L"\\System32\\drivers\\" + strPath;
		}
		return strPath;
	}

	// a file's FileDescription version resource, or empty
	inline std::wstring GetFileDescription(const std::wstring& strPath)
	{
		DWORD dwHandle = 0;
		const DWORD dwSize = GetFileVersionInfoSizeW(strPath.c_str(), &dwHandle);
		if (!dwSize)
		{
			return std::wstring();
		}
		std::vector<BYTE> vData(dwSize);
		if (!GetFileVersionInfoW(strPath.c_str(), 0, dwSize, vData.data()))
		{
			return std::wstring();
		}
		struct LangCodePage { WORD wLanguage; WORD wCodePage; } *pTranslations = nullptr;
		UINT nLength = 0;
		std::vector<std::wstring> vQueries;
		if (VerQueryValueW(vData.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&pTranslations), &nLength) && nLength >= sizeof(LangCodePage))
		{
			WCHAR wszQuery[64];
			swprintf_s(wszQuery, L"\\StringFileInfo\\%04x%04x\\FileDescription", pTranslations[0].wLanguage, pTranslations[0].wCodePage);
			vQueries.push_back(wszQuery);
		}
		vQueries.push_back(L"\\StringFileInfo\\040904b0\\FileDescription");
		vQueries.push_back(L"\\StringFileInfo\\040904e4\\FileDescription");
		for (const auto& strQuery : vQueries)
		{
			WCHAR* pwszValue = nullptr;
			if (VerQueryValueW(vData.data(), strQuery.c_str(), reinterpret_cast<void**>(&pwszValue), &nLength) && pwszValue && nLength > 1)
			{
				return std::wstring(pwszValue);
			}
		}
		return std::wstring();
	}

	// loaded kernel modules, sorted by base address; empty if kernel addresses are hidden from us
	struct KernelModule
	{
		ULONGLONG nBase;
		ULONGLONG nEnd;
		std::wstring strKernelPath;
		std::wstring strName;
	};

	inline std::vector<KernelModule> GetKernelModules()
	{
		struct RTL_PROCESS_MODULE_INFORMATION_
		{
			HANDLE Section;
			PVOID MappedBase;
			PVOID ImageBase;
			ULONG ImageSize;
			ULONG Flags;
			USHORT LoadOrderIndex;
			USHORT InitOrderIndex;
			USHORT LoadCount;
			USHORT OffsetToFileName;
			UCHAR FullPathName[256];
		};
		typedef LONG(NTAPI* NtQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);
		constexpr ULONG SystemModuleInformation = 11;

		std::vector<KernelModule> vModules;
		static const auto pNtQuerySystemInformation = reinterpret_cast<NtQuerySystemInformation_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
		if (!pNtQuerySystemInformation)
		{
			return vModules;
		}
		std::vector<BYTE> vBuffer(256 * 1024);
		ULONG nNeeded = 0;
		LONG status = pNtQuerySystemInformation(SystemModuleInformation, vBuffer.data(), static_cast<ULONG>(vBuffer.size()), &nNeeded);
		if (status < 0 && nNeeded > vBuffer.size())
		{
			vBuffer.resize(nNeeded + 16 * 1024);
			status = pNtQuerySystemInformation(SystemModuleInformation, vBuffer.data(), static_cast<ULONG>(vBuffer.size()), &nNeeded);
		}
		if (status < 0)
		{
			return vModules;
		}
		const ULONG nCount = *reinterpret_cast<const ULONG*>(vBuffer.data());
		const auto* pModules = reinterpret_cast<const RTL_PROCESS_MODULE_INFORMATION_*>(vBuffer.data() + sizeof(ULONG_PTR));
		for (ULONG n = 0; n < nCount; n++)
		{
			const auto& info = pModules[n];
			if (!info.ImageBase || !info.ImageSize)
			{
				continue;
			}
			KernelModule module;
			module.nBase = reinterpret_cast<ULONG_PTR>(info.ImageBase);
			module.nEnd = module.nBase + info.ImageSize;
			const char* pszPath = reinterpret_cast<const char*>(info.FullPathName);
			const size_t nPathLength = strnlen(pszPath, sizeof(info.FullPathName));
			const int nWide = MultiByteToWideChar(CP_ACP, 0, pszPath, static_cast<int>(nPathLength), nullptr, 0);
			module.strKernelPath.resize(nWide);
			MultiByteToWideChar(CP_ACP, 0, pszPath, static_cast<int>(nPathLength), &module.strKernelPath[0], nWide);
			const size_t nSlash = module.strKernelPath.find_last_of(L'\\');
			module.strName = nSlash == std::wstring::npos ? module.strKernelPath : module.strKernelPath.substr(nSlash + 1);
			vModules.push_back(std::move(module));
		}
		std::sort(vModules.begin(), vModules.end(), [](const KernelModule& a, const KernelModule& b) { return a.nBase < b.nBase; });
		return vModules;
	}

	// stops a session, by name; returns true if one was running
	inline bool StopSessionByName(const WCHAR* pwszName)
	{
		std::vector<BYTE> vProperties(sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(WCHAR));
		auto* pProperties = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(vProperties.data());
		pProperties->Wnode.BufferSize = static_cast<ULONG>(vProperties.size());
		pProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
		pProperties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024 * sizeof(WCHAR);
		return ERROR_SUCCESS == ControlTraceW(0, pwszName, pProperties, EVENT_TRACE_CONTROL_STOP);
	}

	// the Windows 8+ private session left running by a Process Lasso that ended without stopping it (uninstall, startup);
	// the Windows 7 kernel logger is only stopped if the marker under hkeyMarkerParent says we started it
	inline void StopStaleSession(const HKEY hkeyMarkerRoot = HKEY_CURRENT_USER, const WCHAR* pwszMarkerParent = nullptr)
	{
		StopSessionByName(SessionName);
		if (pwszMarkerParent)
		{
			HKEY hKey = nullptr;
			if (ERROR_SUCCESS == RegOpenKeyExW(hkeyMarkerRoot, pwszMarkerParent, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey))
			{
				if (ERROR_SUCCESS == RegQueryValueExW(hKey, KernelLoggerOwnerValueName, nullptr, nullptr, nullptr, nullptr))
				{
					StopSessionByName(KERNEL_LOGGER_NAMEW);
					RegDeleteValueW(hKey, KernelLoggerOwnerValueName);
				}
				RegCloseKey(hKey);
			}
		}
	}

	//
	// Monitor
	//

	class Monitor
	{
	public:
		// pwszMarkerParent: an existing, non-volatile HKCU key for the Windows 7 kernel logger ownership marker (a volatile subkey)
		explicit Monitor(const WCHAR* pwszMarkerParent = L"Software\\ProcessLasso") : m_strMarkerParent(pwszMarkerParent ? pwszMarkerParent : L"")
		{
			InitializeSRWLock(&m_lock);
			LARGE_INTEGER li;
			QueryPerformanceFrequency(&li);
			m_nFrequency = static_cast<ULONGLONG>(li.QuadPart);
			m_nTicksPerUs = (std::max)(1ULL, m_nFrequency / 1000000);
			m_nWarningTicks = WarningExecutionUs * m_nFrequency / 1000000;
			m_nBadTicks = BadExecutionUs * m_nFrequency / 1000000;
			m_nWakeWarningTicks = WarningWakeUs * m_nFrequency / 1000000;
			m_nWakeBadTicks = BadWakeUs * m_nFrequency / 1000000;
			m_bWindows8 = IsWindows8OrLater();
		}

		~Monitor()
		{
			Stop();
		}

		Monitor(const Monitor&) = delete;
		Monitor& operator=(const Monitor&) = delete;

		bool IsRunning() const
		{
			return m_bRunning;
		}

		DWORD GetLastError() const
		{
			return m_dwLastError;
		}

		// starts (or resumes) the session, consumer and timer wake threads
		StartResult Start(const bool bHardFaults)
		{
			if (m_bRunning)
			{
				return StartOK;
			}
			EnablePrivilege(SE_DEBUG_NAME);	// kernel module addresses are hidden without it on Windows 11 24H2+
			EnablePrivilege(SE_SYSTEM_PROFILE_NAME);
			RefreshModules(true);

			const StartResult result = StartSession(bHardFaults);
			if (StartOK != result)
			{
				return result;
			}

			m_bHardFaults = bHardFaults;
			m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			LARGE_INTEGER li;
			QueryPerformanceCounter(&li);
			{
				SRWLockExclusive lock(m_lock);
				m_nRunStart = static_cast<ULONGLONG>(li.QuadPart);
			}
			m_bRunning = true;
			m_hConsumerThread = CreateThread(nullptr, 0, &Monitor::ConsumerThread, this, 0, nullptr);
			DWORD dwTimerThreadId = 0;
			m_hTimerThread = CreateThread(nullptr, 0, &Monitor::TimerThread, this, CREATE_SUSPENDED, &dwTimerThreadId);
			if (m_hTimerThread)
			{
				m_dwTimerThreadId = dwTimerThreadId;
				ResumeThread(m_hTimerThread);
			}
			if (!m_hConsumerThread || !m_hTimerThread)
			{
				m_dwLastError = ::GetLastError();
				Stop();
				return StartFailed;
			}
			return StartOK;
		}

		// stops the session and threads; the results are kept
		void Stop()
		{
			if (!m_bRunning && !m_hSession && !m_hConsumerThread && !m_hTimerThread)
			{
				return;
			}
			QueryLost();
			StopSession();
			if (m_hStopEvent)
			{
				SetEvent(m_hStopEvent);
			}
			for (HANDLE* phThread : { &m_hConsumerThread, &m_hTimerThread })
			{
				if (*phThread)
				{
					WaitForSingleObject(*phThread, 10000);
					CloseHandle(*phThread);
					*phThread = nullptr;
				}
			}
			if (m_hTrace && INVALID_PROCESSTRACE_HANDLE != m_hTrace)
			{
				CloseTrace(m_hTrace);
			}
			m_hTrace = INVALID_PROCESSTRACE_HANDLE;
			if (m_hStopEvent)
			{
				CloseHandle(m_hStopEvent);
				m_hStopEvent = nullptr;
			}
			if (m_bRunning)
			{
				LARGE_INTEGER li;
				QueryPerformanceCounter(&li);
				SRWLockExclusive lock(m_lock);
				MatchWakes(~0ULL);	// before the elapsed time moves on, so the history buckets line up
				m_nElapsedBefore += static_cast<ULONGLONG>(li.QuadPart) - m_nRunStart;
				m_nRunStart = 0;
				m_setReady.clear();
				m_dqWakes.clear();
				m_nPreviousWake = 0;
			}
			m_dwTimerThreadId = 0;
			m_bRunning = false;
		}

		// clears the results; a running test keeps running
		void Reset()
		{
			LARGE_INTEGER li;
			QueryPerformanceCounter(&li);
			SRWLockExclusive lock(m_lock);
			m_isr = ExecutionStats();
			m_dpc = ExecutionStats();
			m_wake = ExecutionStats();
			for (auto& module : m_vModuleStats)
			{
				module.isr = ExecutionStats();
				module.dpc = ExecutionStats();
			}
			m_vRetiredModules.clear();
			m_unknownModule.isr = ExecutionStats();
			m_unknownModule.dpc = ExecutionStats();
			m_vCPUs.clear();
			m_mapProcesses.clear();
			m_nHardFaults = 0;
			m_nUnattributed = 0;
			m_dqHistory.clear();
			m_setReady.clear();
			m_dqWakes.clear();
			m_nPreviousWake = 0;
			m_nHistoryFirstSecond = 0;
			memset(m_perfInfoOpcodes, 0, sizeof(m_perfInfoOpcodes));
			m_nElapsedBefore = 0;
			m_nEventsLostBefore = m_nBuffersLostBefore = 0;
			m_nEventsLostBase = m_nEventsLostCurrent;
			m_nBuffersLostBase = m_nBuffersLostCurrent;
			if (m_bRunning)
			{
				m_nRunStart = static_cast<ULONGLONG>(li.QuadPart);
			}
		}

		void GetSnapshot(Snapshot& snapshot)
		{
			if (m_bRunning)
			{
				QueryLost();
			}
			LARGE_INTEGER li;
			QueryPerformanceCounter(&li);
			SRWLockShared lock(m_lock);
			snapshot = Snapshot();
			snapshot.nFrequency = m_nFrequency;
			snapshot.nElapsedTicks = m_nElapsedBefore + (m_bRunning && m_nRunStart ? static_cast<ULONGLONG>(li.QuadPart) - m_nRunStart : 0);
			snapshot.bRunning = m_bRunning;
			snapshot.bHardFaults = m_bHardFaults;
			snapshot.bHighResolutionTimer = m_bHighResolutionTimer;
			snapshot.bKernelLogger = !m_bWindows8;
			snapshot.nEventsLost = m_nEventsLostBefore + (m_nEventsLostCurrent - m_nEventsLostBase);
			snapshot.nBuffersLost = m_nBuffersLostBefore + (m_nBuffersLostCurrent - m_nBuffersLostBase);
			snapshot.nUnattributed = m_nUnattributed;
			snapshot.isr = m_isr;
			snapshot.dpc = m_dpc;
			snapshot.wake = m_wake;
			snapshot.nHardFaults = m_nHardFaults;
			for (const auto& module : m_vModuleStats)
			{
				if (module.isr.nCount || module.dpc.nCount)
				{
					snapshot.vModules.push_back(module);
				}
			}
			snapshot.vModules.insert(snapshot.vModules.end(), m_vRetiredModules.begin(), m_vRetiredModules.end());
			if (m_unknownModule.isr.nCount || m_unknownModule.dpc.nCount)
			{
				snapshot.vModules.push_back(m_unknownModule);
			}
			snapshot.vCPUs = m_vCPUs;
			const DWORD dwProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
			if (snapshot.vCPUs.size() < dwProcessors)
			{
				snapshot.vCPUs.resize(dwProcessors);
			}
			for (const auto& entry : m_mapProcesses)
			{
				snapshot.vProcesses.push_back(entry.second);
			}
			snapshot.vHistory.assign(m_dqHistory.begin(), m_dqHistory.end());
			memcpy(snapshot.perfInfoOpcodes, m_perfInfoOpcodes, sizeof(m_perfInfoOpcodes));
		}

	private:
		struct SRWLockExclusive
		{
			explicit SRWLockExclusive(SRWLOCK& lock) : m_lock(lock) { AcquireSRWLockExclusive(&m_lock); }
			~SRWLockExclusive() { ReleaseSRWLockExclusive(&m_lock); }
			SRWLOCK& m_lock;
		};

		struct SRWLockShared
		{
			explicit SRWLockShared(SRWLOCK& lock) : m_lock(lock) { AcquireSRWLockShared(&m_lock); }
			~SRWLockShared() { ReleaseSRWLockShared(&m_lock); }
			SRWLOCK& m_lock;
		};

		//
		// session
		//

		struct PropertiesBuffer
		{
			std::vector<BYTE> vData;
			EVENT_TRACE_PROPERTIES* Get() { return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(vData.data()); }
		};

		static PropertiesBuffer MakeProperties()
		{
			PropertiesBuffer buffer;
			buffer.vData.resize(sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(WCHAR));
			auto* p = buffer.Get();
			p->Wnode.BufferSize = static_cast<ULONG>(buffer.vData.size());
			p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
			p->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024 * sizeof(WCHAR);
			return buffer;
		}

		const WCHAR* GetSessionName() const
		{
			return m_bWindows8 ? SessionName : KERNEL_LOGGER_NAMEW;
		}

		StartResult StartSession(const bool bHardFaults)
		{
			PropertiesBuffer buffer = MakeProperties();
			auto* p = buffer.Get();
			p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
			p->Wnode.ClientContext = 1;	// QPC
			p->Wnode.Guid = m_bWindows8 ? PrivateSessionGuid : SystemTraceGuid;
			p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | (m_bWindows8 ? EVENT_TRACE_SYSTEM_LOGGER_MODE : 0);
			p->BufferSize = 64;		// KB
			p->MinimumBuffers = 32;
			p->MaximumBuffers = 128;
			p->FlushTimer = 1;
			p->EnableFlags = EVENT_TRACE_FLAG_INTERRUPT | EVENT_TRACE_FLAG_DPC | EVENT_TRACE_FLAG_DISPATCHER | (bHardFaults ? EVENT_TRACE_FLAG_MEMORY_HARD_FAULTS : 0);

			if (m_bWindows8)
			{
				// one of ours left by a process that ended without stopping it
				StopSessionByName(SessionName);
			}
			else
			{
				StopStaleSession(HKEY_CURRENT_USER, m_strMarkerParent.empty() ? nullptr : m_strMarkerParent.c_str());
			}

			TRACEHANDLE hSession = 0;
			ULONG nResult = StartTraceW(&hSession, GetSessionName(), p);
			if (ERROR_SUCCESS != nResult)
			{
				m_dwLastError = nResult;
				switch (nResult)
				{
				case ERROR_ACCESS_DENIED:
					return StartAccessDenied;
				case ERROR_ALREADY_EXISTS:
					return m_bWindows8 ? StartFailed : StartKernelLoggerInUse;
				case ERROR_NO_SYSTEM_RESOURCES:
					return StartNoResources;
				default:
					return StartFailed;
				}
			}
			m_hSession = hSession;
			if (!m_bWindows8)
			{
				SetKernelLoggerMarker(true);
			}

			EVENT_TRACE_LOGFILEW logFile = {};
			logFile.LoggerName = const_cast<LPWSTR>(GetSessionName());
			logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
			logFile.EventRecordCallback = &Monitor::EventRecordCallback;
			logFile.Context = this;
			m_hTrace = OpenTraceW(&logFile);
			if (INVALID_PROCESSTRACE_HANDLE == m_hTrace)
			{
				m_dwLastError = ::GetLastError();
				StopSession();
				return StartFailed;
			}
			{
				SRWLockExclusive lock(m_lock);
				m_nEventsLostBase = m_nEventsLostCurrent = 0;
				m_nBuffersLostBase = m_nBuffersLostCurrent = 0;
			}
			return StartOK;
		}

		void StopSession()
		{
			if (!m_hSession)
			{
				return;
			}
			PropertiesBuffer buffer = MakeProperties();
			ControlTraceW(m_hSession, nullptr, buffer.Get(), EVENT_TRACE_CONTROL_STOP);
			m_hSession = 0;
			if (!m_bWindows8)
			{
				SetKernelLoggerMarker(false);
			}
			SRWLockExclusive lock(m_lock);
			m_nEventsLostBefore += m_nEventsLostCurrent - m_nEventsLostBase;
			m_nBuffersLostBefore += m_nBuffersLostCurrent - m_nBuffersLostBase;
			m_nEventsLostBase = m_nEventsLostCurrent = 0;
			m_nBuffersLostBase = m_nBuffersLostCurrent = 0;
		}

		void QueryLost()
		{
			if (!m_hSession)
			{
				return;
			}
			PropertiesBuffer buffer = MakeProperties();
			if (ERROR_SUCCESS == ControlTraceW(m_hSession, nullptr, buffer.Get(), EVENT_TRACE_CONTROL_QUERY))
			{
				SRWLockExclusive lock(m_lock);
				m_nEventsLostCurrent = buffer.Get()->EventsLost;
				m_nBuffersLostCurrent = buffer.Get()->RealTimeBuffersLost;
			}
		}

		void SetKernelLoggerMarker(const bool bSet)
		{
			if (m_strMarkerParent.empty())
			{
				return;
			}
			HKEY hKey = nullptr;
			if (ERROR_SUCCESS == RegCreateKeyExW(HKEY_CURRENT_USER, m_strMarkerParent.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr))
			{
				if (bSet)
				{
					const DWORD dwValue = GetCurrentProcessId();
					RegSetValueExW(hKey, KernelLoggerOwnerValueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwValue), sizeof(dwValue));
				}
				else
				{
					RegDeleteValueW(hKey, KernelLoggerOwnerValueName);
				}
				RegCloseKey(hKey);
			}
		}

		//
		// consumer
		//

		static DWORD WINAPI ConsumerThread(LPVOID pParam)
		{
			Monitor* pThis = static_cast<Monitor*>(pParam);
			TRACEHANDLE hTrace = pThis->m_hTrace;
			ProcessTrace(&hTrace, 1, nullptr, nullptr);	// returns when the session stops
			return 0;
		}

		static VOID WINAPI EventRecordCallback(PEVENT_RECORD pEvent)
		{
			static_cast<Monitor*>(pEvent->UserContext)->OnEvent(pEvent);
		}

		static USHORT GetProcessorIndex(const EVENT_RECORD* pEvent)
		{
			return (pEvent->EventHeader.Flags & EVENT_HEADER_FLAG_PROCESSOR_INDEX) ? pEvent->BufferContext.ProcessorIndex : pEvent->BufferContext.ProcessorNumber;
		}

		static size_t GetPointerSize(const EVENT_RECORD* pEvent)
		{
			return (pEvent->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) ? 4 : 8;
		}

		static ULONGLONG ReadPointer(const BYTE* p, const size_t nSize)
		{
			if (4 == nSize)
			{
				ULONG n;
				memcpy(&n, p, sizeof(n));
				return n;
			}
			ULONGLONG n;
			memcpy(&n, p, sizeof(n));
			return n;
		}

		void OnEvent(const EVENT_RECORD* pEvent)
		{
			const UCHAR nOpcode = pEvent->EventHeader.EventDescriptor.Opcode;
			if (IsEqualGUID(pEvent->EventHeader.ProviderId, PerfInfoGuid))
			{
				InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&m_perfInfoOpcodes[nOpcode]));
				const bool bIsr = OpcodeIsr == nOpcode || OpcodeIsrMsi == nOpcode;
				const bool bDpc = OpcodeDpc == nOpcode || OpcodeThreadedDpc == nOpcode || OpcodeTimerDpc == nOpcode;
				if (!bIsr && !bDpc)
				{
					return;
				}
				// InitialTime (8 bytes), Routine (pointer) lead every ISR and DPC class
				const size_t nPointer = GetPointerSize(pEvent);
				if (pEvent->UserDataLength < 8 + nPointer)
				{
					return;
				}
				const BYTE* pData = static_cast<const BYTE*>(pEvent->UserData);
				ULONGLONG nInitialTime;
				memcpy(&nInitialTime, pData, sizeof(nInitialTime));
				const ULONGLONG nRoutine = ReadPointer(pData + 8, nPointer);
				const ULONGLONG nEnd = static_cast<ULONGLONG>(pEvent->EventHeader.TimeStamp.QuadPart);
				if (nEnd < nInitialTime || nEnd - nInitialTime > m_nFrequency)
				{
					return;	// clock mismatch or garbage; a real ISR/DPC never runs for a second
				}
				OnExecution(bIsr, nRoutine, nEnd - nInitialTime, nEnd, GetProcessorIndex(pEvent));
			}
			else if (IsEqualGUID(pEvent->EventHeader.ProviderId, PageFaultGuid) && OpcodeHardFault == nOpcode)
			{
				// InitialTime (8), ReadOffset (8), VirtualAddress (pointer), FileObject (pointer), TThreadId (4), ByteCount (4)
				const size_t nPointer = GetPointerSize(pEvent);
				const size_t nOffset = 16 + 2 * nPointer;
				if (pEvent->UserDataLength < nOffset + 4)
				{
					return;
				}
				ULONG nThreadId;
				memcpy(&nThreadId, static_cast<const BYTE*>(pEvent->UserData) + nOffset, sizeof(nThreadId));
				OnHardFault(nThreadId);
			}
			else if (IsEqualGUID(pEvent->EventHeader.ProviderId, ThreadGuid) && OpcodeReadyThread == nOpcode)
			{
				// TThreadId (4 bytes) leads the ReadyThread class
				const DWORD dwTimerThreadId = m_dwTimerThreadId;
				if (!dwTimerThreadId || pEvent->UserDataLength < 4)
				{
					return;
				}
				ULONG nThreadId;
				memcpy(&nThreadId, pEvent->UserData, sizeof(nThreadId));
				if (nThreadId == dwTimerThreadId)
				{
					SRWLockExclusive lock(m_lock);
					m_setReady.insert(static_cast<ULONGLONG>(pEvent->EventHeader.TimeStamp.QuadPart));
				}
			}
		}

		// index into m_vModuleStats, or -1; call with the lock held
		int FindModule(const ULONGLONG nAddress) const
		{
			auto it = std::upper_bound(m_vModules.begin(), m_vModules.end(), nAddress, [](const ULONGLONG n, const KernelModule& m) { return n < m.nBase; });
			if (it == m_vModules.begin())
			{
				return -1;
			}
			--it;
			return nAddress < it->nEnd ? static_cast<int>(it - m_vModules.begin()) : -1;
		}

		// reloads the module list (rate limited unless bForce), keeping the statistics of modules already seen
		void RefreshModules(const bool bForce)
		{
			const ULONGLONG nNow = GetTickCount64();
			if (!bForce)
			{
				SRWLockShared lock(m_lock);
				if (nNow - m_nLastModuleRefresh < (m_vModules.empty() ? 30000ULL : 2000ULL))
				{
					return;
				}
			}
			std::vector<KernelModule> vModules = GetKernelModules();
			SRWLockExclusive lock(m_lock);
			m_nLastModuleRefresh = nNow;
			std::vector<ModuleStats> vStats(vModules.size());
			for (size_t n = 0; n < vModules.size(); n++)
			{
				ModuleStats& stats = vStats[n];
				stats.strName = vModules[n].strName;
				stats.strPath = KernelPathToWin32(vModules[n].strKernelPath);
				stats.nBase = vModules[n].nBase;
				stats.nSize = vModules[n].nEnd - vModules[n].nBase;
				for (const auto& old : m_vModuleStats)
				{
					if (old.nBase == stats.nBase && 0 == _wcsicmp(old.strName.c_str(), stats.strName.c_str()))
					{
						stats.isr = old.isr;
						stats.dpc = old.dpc;
						break;
					}
				}
			}
			// an unloaded module's results stay visible
			for (const auto& old : m_vModuleStats)
			{
				if ((old.isr.nCount || old.dpc.nCount) && std::none_of(vStats.begin(), vStats.end(),
					[&](const ModuleStats& s) { return s.nBase == old.nBase && 0 == _wcsicmp(s.strName.c_str(), old.strName.c_str()); }))
				{
					m_vRetiredModules.push_back(old);
				}
			}
			m_vModules = std::move(vModules);
			m_vModuleStats = std::move(vStats);
			if (m_unknownModule.strName.empty())
			{
				m_unknownModule.strName = L"?";
			}
		}

		CPUStats& GetCPU(const USHORT nProcessor)
		{
			if (nProcessor >= m_vCPUs.size())
			{
				m_vCPUs.resize(nProcessor + 1);
			}
			return m_vCPUs[nProcessor];
		}

		// the history bucket for an event at nTime (QPC); call with the lock held
		SecondBucket* GetBucket(const ULONGLONG nTime)
		{
			if (!m_nRunStart || nTime < m_nRunStart)
			{
				return nullptr;
			}
			const ULONGLONG nSecond = (m_nElapsedBefore + (nTime - m_nRunStart)) / m_nFrequency;
			if (m_dqHistory.empty())
			{
				m_nHistoryFirstSecond = nSecond;
			}
			if (nSecond < m_nHistoryFirstSecond)
			{
				return nullptr;
			}
			while (m_nHistoryFirstSecond + m_dqHistory.size() <= nSecond)
			{
				m_dqHistory.emplace_back();
				if (m_dqHistory.size() > HistoryLengthSeconds)
				{
					m_dqHistory.pop_front();
					m_nHistoryFirstSecond++;
				}
			}
			return &m_dqHistory[static_cast<size_t>(nSecond - m_nHistoryFirstSecond)];
		}

		void OnExecution(const bool bIsr, const ULONGLONG nRoutine, const ULONGLONG nTicks, const ULONGLONG nTime, const USHORT nProcessor)
		{
			bool bUnknown = false;
			{
				SRWLockExclusive lock(m_lock);
				const int nModule = FindModule(nRoutine);
				ModuleStats& module = nModule >= 0 ? m_vModuleStats[nModule] : m_unknownModule;
				bUnknown = nModule < 0;
				CPUStats& cpu = GetCPU(nProcessor);
				if (bIsr)
				{
					module.isr.Add(nTicks, m_nTicksPerUs, m_nWarningTicks, m_nBadTicks);
					cpu.isr.Add(nTicks, m_nTicksPerUs, m_nWarningTicks, m_nBadTicks);
					m_isr.Add(nTicks, m_nTicksPerUs, m_nWarningTicks, m_nBadTicks);
				}
				else
				{
					module.dpc.Add(nTicks, m_nTicksPerUs, m_nWarningTicks, m_nBadTicks);
					cpu.dpc.Add(nTicks, m_nTicksPerUs, m_nWarningTicks, m_nBadTicks);
					m_dpc.Add(nTicks, m_nTicksPerUs, m_nWarningTicks, m_nBadTicks);
				}
				if (bUnknown)
				{
					m_nUnattributed++;
				}
				if (SecondBucket* pBucket = GetBucket(nTime))
				{
					if (bIsr)
					{
						pBucket->nIsrCount++;
						pBucket->nIsrTicks += nTicks;
						pBucket->nMaxIsrTicks = (std::max)(pBucket->nMaxIsrTicks, nTicks);
					}
					else
					{
						pBucket->nDpcCount++;
						pBucket->nDpcTicks += nTicks;
						pBucket->nMaxDpcTicks = (std::max)(pBucket->nMaxDpcTicks, nTicks);
					}
				}
			}
			if (bUnknown)
			{
				// a driver loaded after the last refresh; the next events from it are attributed
				RefreshModules(false);
			}
		}

		DWORD GetProcessIdOfThreadCached(const DWORD dwThreadId)
		{
			auto it = m_mapThreadToProcess.find(dwThreadId);
			if (it != m_mapThreadToProcess.end())
			{
				return it->second;
			}
			if (m_mapThreadToProcess.size() > 16384)
			{
				m_mapThreadToProcess.clear();
			}
			DWORD dwProcessId = static_cast<DWORD>(-1);
			if (HANDLE hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, dwThreadId))
			{
				dwProcessId = GetProcessIdOfThread(hThread);
				CloseHandle(hThread);
			}
			m_mapThreadToProcess[dwThreadId] = dwProcessId;
			return dwProcessId;
		}

		static std::wstring GetProcessName(const DWORD dwProcessId)
		{
			if (0 == dwProcessId)
			{
				return L"Idle";
			}
			if (4 == dwProcessId)
			{
				return L"System";
			}
			std::wstring strName;
			if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId))
			{
				WCHAR wszPath[MAX_PATH * 2] = {};
				DWORD dwLength = _countof(wszPath);
				if (QueryFullProcessImageNameW(hProcess, 0, wszPath, &dwLength))
				{
					const WCHAR* pwszName = wcsrchr(wszPath, L'\\');
					strName = pwszName ? pwszName + 1 : wszPath;
				}
				CloseHandle(hProcess);
			}
			return strName;
		}

		void OnHardFault(const DWORD dwThreadId)
		{
			// only the consumer thread touches the thread map, so the lookups run without the lock
			const DWORD dwProcessId = GetProcessIdOfThreadCached(dwThreadId);
			bool bNew = false;
			{
				SRWLockExclusive lock(m_lock);
				m_nHardFaults++;
				auto it = m_mapProcesses.find(dwProcessId);
				if (it == m_mapProcesses.end())
				{
					bNew = true;
				}
				else
				{
					it->second.nHardFaults++;
				}
			}
			if (bNew)
			{
				ProcessFaults entry;
				entry.dwProcessId = dwProcessId;
				entry.strName = static_cast<DWORD>(-1) == dwProcessId ? std::wstring() : GetProcessName(dwProcessId);
				entry.nHardFaults = 1;
				SRWLockExclusive lock(m_lock);
				auto result = m_mapProcesses.emplace(dwProcessId, entry);
				if (!result.second)
				{
					result.first->second.nHardFaults++;
				}
			}
		}

		//
		// timer wake latency
		//

		static DWORD WINAPI TimerThread(LPVOID pParam)
		{
			static_cast<Monitor*>(pParam)->TimerLoop();
			return 0;
		}

		void TimerLoop()
		{
			// the high resolution timer samples about 1000 times a second; without it (before Windows 10 1803) the wait
			// ends on the next system timer tick, which only lowers the sample rate
			HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
			m_bHighResolutionTimer = nullptr != hTimer;
			if (!hTimer)
			{
				hTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
				if (!hTimer)
				{
					return;
				}
			}
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
			const HANDLE hHandles[] = { m_hStopEvent, hTimer };
			for (;;)
			{
				LARGE_INTEGER liDue;
				liDue.QuadPart = -10000;	// 1 ms, relative
				if (!SetWaitableTimer(hTimer, &liDue, 0, nullptr, nullptr, FALSE))
				{
					break;
				}
				if (WAIT_OBJECT_0 + 1 != WaitForMultipleObjects(_countof(hHandles), hHandles, FALSE, INFINITE))
				{
					break;
				}
				LARGE_INTEGER liWake;
				QueryPerformanceCounter(&liWake);
				PROCESSOR_NUMBER processor = {};
				GetCurrentProcessorNumberEx(&processor);
				SRWLockExclusive lock(m_lock);
				m_dqWakes.push_back({ static_cast<ULONGLONG>(liWake.QuadPart), GetSystemProcessorIndex(processor) });
				MatchWakes(static_cast<ULONGLONG>(liWake.QuadPart) - (std::min)(static_cast<ULONGLONG>(liWake.QuadPart), MatchDelaySeconds * m_nFrequency));
			}
			CancelWaitableTimer(hTimer);
			CloseHandle(hTimer);
		}

		// pairs each wake of the wake thread (up to nHorizon, QPC) with the last ReadyThread event before it; call with the lock held.
		// ETW delivers events from per-processor buffers, late and out of order, so a wake is only paired once every event up to
		// its time has arrived: MatchDelaySeconds behind while running, and all of them once the session has stopped.
		void MatchWakes(const ULONGLONG nHorizon)
		{
			while (!m_dqWakes.empty() && m_dqWakes.front().nTime <= nHorizon)
			{
				const PendingWake wake = m_dqWakes.front();
				m_dqWakes.pop_front();
				auto itAfter = m_setReady.upper_bound(wake.nTime);
				if (itAfter != m_setReady.begin())
				{
					const ULONGLONG nReady = *std::prev(itAfter);
					// a ready event at or before the previous wake belongs to that wake: this one's was lost
					if (nReady > m_nPreviousWake)
					{
						const ULONGLONG nLatency = wake.nTime - nReady;
						m_wake.Add(nLatency, m_nTicksPerUs, m_nWakeWarningTicks, m_nWakeBadTicks);
						GetCPU(wake.nProcessor).wake.Add(nLatency, m_nTicksPerUs, m_nWakeWarningTicks, m_nWakeBadTicks);
						if (SecondBucket* pBucket = GetBucket(wake.nTime))
						{
							pBucket->nMaxWakeTicks = (std::max)(pBucket->nMaxWakeTicks, nLatency);
						}
					}
				}
				m_setReady.erase(m_setReady.begin(), itAfter);
				m_nPreviousWake = wake.nTime;
			}
			// without ready events (not delivered) the wakes would pile up
			while (m_dqWakes.size() > 16384)
			{
				m_dqWakes.pop_front();
			}
		}

		SRWLOCK m_lock;
		std::wstring m_strMarkerParent;
		bool m_bWindows8 = true;
		volatile bool m_bRunning = false;
		bool m_bHardFaults = false;
		volatile bool m_bHighResolutionTimer = true;
		volatile DWORD m_dwTimerThreadId = 0;
		DWORD m_dwLastError = 0;

		TRACEHANDLE m_hSession = 0;
		TRACEHANDLE m_hTrace = INVALID_PROCESSTRACE_HANDLE;
		HANDLE m_hConsumerThread = nullptr;
		HANDLE m_hTimerThread = nullptr;
		HANDLE m_hStopEvent = nullptr;

		ULONGLONG m_nFrequency = 1;
		ULONGLONG m_nTicksPerUs = 1;
		ULONGLONG m_nWarningTicks = 0;
		ULONGLONG m_nBadTicks = 0;
		ULONGLONG m_nWakeWarningTicks = 0;
		ULONGLONG m_nWakeBadTicks = 0;

		// guarded by m_lock
		ULONGLONG m_nRunStart = 0;		// QPC when the current run started, 0 when stopped
		ULONGLONG m_nElapsedBefore = 0;	// running time of earlier runs
		std::vector<KernelModule> m_vModules;
		std::vector<ModuleStats> m_vModuleStats;	// parallel to m_vModules
		std::vector<ModuleStats> m_vRetiredModules;	// unloaded modules with results (reported as is)
		ModuleStats m_unknownModule;
		std::vector<CPUStats> m_vCPUs;
		std::map<DWORD, ProcessFaults> m_mapProcesses;
		ExecutionStats m_isr;
		ExecutionStats m_dpc;
		ExecutionStats m_wake;
		ULONGLONG m_nHardFaults = 0;
		ULONGLONG m_nUnattributed = 0;
		std::deque<SecondBucket> m_dqHistory;
		ULONGLONG m_nHistoryFirstSecond = 0;
		ULONGLONG m_perfInfoOpcodes[256] = {};
		ULONGLONG m_nEventsLostCurrent = 0, m_nEventsLostBase = 0, m_nEventsLostBefore = 0;
		ULONGLONG m_nBuffersLostCurrent = 0, m_nBuffersLostBase = 0, m_nBuffersLostBefore = 0;

		ULONGLONG m_nLastModuleRefresh = 0;	// guarded by m_lock
		struct PendingWake
		{
			ULONGLONG nTime;
			USHORT nProcessor;
		};
		std::multiset<ULONGLONG> m_setReady;	// guarded by m_lock: ReadyThread times of the wake thread, not yet paired
		std::deque<PendingWake> m_dqWakes;		// guarded by m_lock: wakes not yet paired
		ULONGLONG m_nPreviousWake = 0;			// guarded by m_lock

		// consumer thread only
		std::unordered_map<DWORD, DWORD> m_mapThreadToProcess;
	};
}
