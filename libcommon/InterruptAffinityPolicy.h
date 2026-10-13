#pragma once

// Per-device interrupt affinity policy and MSI mode, as stored under the device hardware key:
//
//   HKLM\SYSTEM\CurrentControlSet\Enum\<instance id>\Device Parameters\Interrupt Management\Affinity Policy
//     DevicePolicy (REG_DWORD, IRQ_DEVICE_POLICY), AssignmentSetOverride (REG_BINARY, KAFFINITY), DevicePriority (REG_DWORD, IRQ_PRIORITY)
//   ...\Interrupt Management\MessageSignaledInterruptProperties
//     MSISupported (REG_DWORD), MessageNumberLimit (REG_DWORD)
//
// Values are read when the device's resources are assigned, so a change applies after the device restarts or after a reboot.
// The effective result is read back from the allocated configuration (IRQ resource descriptors).

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace InterruptAffinityPolicy
{
	// IRQ_DEVICE_POLICY (wdm.h)
	enum DevicePolicy : DWORD
	{
		PolicyMachineDefault = 0,
		PolicyAllCloseProcessors = 1,
		PolicyOneCloseProcessor = 2,
		PolicyAllProcessorsInMachine = 3,
		PolicySpecifiedProcessors = 4,
		PolicySpreadMessagesAcrossAllProcessors = 5,
		PolicyAllProcessorsInGroupWhenSteered = 6,
	};

	// IRQ_PRIORITY (wdm.h)
	enum DevicePriority : DWORD
	{
		PriorityUndefined = 0,
		PriorityLow = 1,
		PriorityNormal = 2,
		PriorityHigh = 3,
	};

	enum InterruptType
	{
		InterruptNone,
		InterruptLine,
		InterruptMessage,	// MSI or MSI-X; allocated message interrupts report an IRQ number above 0x7fffffff
	};

	// the registry settings of a device; each value is optional (absent means the driver or system default)
	struct Settings
	{
		bool bHasPolicy = false;
		DWORD dwPolicy = PolicyMachineDefault;
		bool bHasAssignmentSetOverride = false;
		ULONGLONG nAssignmentSetOverride = 0;
		bool bHasPriority = false;
		DWORD dwPriority = PriorityUndefined;
		bool bHasMSISupported = false;
		DWORD dwMSISupported = 0;
		bool bHasMessageNumberLimit = false;
		DWORD dwMessageNumberLimit = 0;

		bool operator==(const Settings& o) const
		{
			return bHasPolicy == o.bHasPolicy && (!bHasPolicy || dwPolicy == o.dwPolicy)
				&& bHasAssignmentSetOverride == o.bHasAssignmentSetOverride && (!bHasAssignmentSetOverride || nAssignmentSetOverride == o.nAssignmentSetOverride)
				&& bHasPriority == o.bHasPriority && (!bHasPriority || dwPriority == o.dwPriority)
				&& bHasMSISupported == o.bHasMSISupported && (!bHasMSISupported || dwMSISupported == o.dwMSISupported)
				&& bHasMessageNumberLimit == o.bHasMessageNumberLimit && (!bHasMessageNumberLimit || dwMessageNumberLimit == o.dwMessageNumberLimit);
		}
		bool operator!=(const Settings& o) const { return !(*this == o); }
	};

	// one allocated interrupt of a device
	struct AllocatedInterrupt
	{
		ULONG nVector = 0;			// IRQ number (line) or message number encoding (message)
		ULONGLONG nAffinity = 0;	// fixed target processors; 0 when the interrupt is dynamically steered (no policy restricts it)
		bool bMessage = false;
	};

	struct Device
	{
		std::wstring strInstanceId;
		std::wstring strName;		// friendly name, else device description
		std::wstring strClass;
		std::wstring strLocation;
		std::vector<AllocatedInterrupt> vInterrupts;
		Settings settings;
		bool bSettingsReadable = false;
		// the kernel's record of the processors it targets for the device, written when the device starts
		// ("Affinity Policy - Temporal"); present for most devices, including steered ones (then all processors)
		bool bHasTargetSet = false;
		USHORT nTargetGroup = 0;
		ULONGLONG nTargetSet = 0;

		InterruptType GetInterruptType() const
		{
			if (vInterrupts.empty())
			{
				return InterruptNone;
			}
			for (const auto& irq : vInterrupts)
			{
				if (!irq.bMessage)
				{
					return InterruptLine;
				}
			}
			return InterruptMessage;
		}
	};

	namespace Detail
	{
		const WCHAR* const AffinityPolicySubkey = L"Interrupt Management\\Affinity Policy";
		const WCHAR* const MSIPropertiesSubkey = L"Interrupt Management\\MessageSignaledInterruptProperties";
		const WCHAR* const TemporalPolicySubkey = L"Interrupt Management\\Affinity Policy - Temporal";	// kernel-owned, read only
		const WCHAR* const DevicePolicyValue = L"DevicePolicy";
		const WCHAR* const AssignmentSetOverrideValue = L"AssignmentSetOverride";
		const WCHAR* const DevicePriorityValue = L"DevicePriority";
		const WCHAR* const MSISupportedValue = L"MSISupported";
		const WCHAR* const MessageNumberLimitValue = L"MessageNumberLimit";

		// IRQ_DES_64 with the NT_PROCESSOR_GROUPS layout (USHORT flags + USHORT group); declared here so the
		// layout does not depend on whether cfgmgr32.h was already included without NT_PROCESSOR_GROUPS.
		// Observed on a single group system (Windows 11 26200): IRQD_Group is 1 for every message interrupt
		// and 0 for line interrupts, so it is not usable as a group number; the target group comes from the
		// kernel's Temporal key instead.
		struct IrqDes64Grouped
		{
			DWORD IRQD_Count;
			DWORD IRQD_Type;
			USHORT IRQD_Flags;
			USHORT IRQD_Group;
			ULONG IRQD_Alloc_Num;
			ULONG64 IRQD_Affinity;
		};
		static_assert(sizeof(IrqDes64Grouped) == sizeof(IRQ_DES_64), "IRQ_DES_64 layout mismatch");

		inline std::wstring GetStringProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfo, DWORD dwProperty)
		{
			WCHAR wszBuffer[1024] = {};
			DWORD dwType = 0;
			if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, dwProperty, &dwType, reinterpret_cast<PBYTE>(wszBuffer), sizeof(wszBuffer) - sizeof(WCHAR), nullptr)
				&& (REG_SZ == dwType || REG_MULTI_SZ == dwType))
			{
				return wszBuffer;
			}
			return std::wstring();
		}

		inline bool ReadDword(HKEY hKey, const WCHAR* pwszSubkey, const WCHAR* pwszValue, DWORD& dwOut)
		{
			DWORD dwSize = sizeof(dwOut);
			DWORD dwType = 0;
			return ERROR_SUCCESS == RegGetValueW(hKey, pwszSubkey, pwszValue, RRF_RT_REG_DWORD, &dwType, &dwOut, &dwSize);
		}

		inline bool ReadAffinity(HKEY hKey, ULONGLONG& nOut)
		{
			BYTE buffer[sizeof(ULONGLONG)] = {};
			DWORD dwSize = sizeof(buffer);
			if (ERROR_SUCCESS != RegGetValueW(hKey, AffinityPolicySubkey, AssignmentSetOverrideValue, RRF_RT_REG_BINARY, nullptr, buffer, &dwSize) || 0 == dwSize)
			{
				return false;
			}
			nOut = 0;
			memcpy(&nOut, buffer, dwSize);	// 4 byte values exist from 32-bit era tools
			return true;
		}

		inline void ReadSettings(HKEY hKey, Settings& settings)
		{
			settings = Settings();
			settings.bHasPolicy = ReadDword(hKey, AffinityPolicySubkey, DevicePolicyValue, settings.dwPolicy);
			settings.bHasAssignmentSetOverride = ReadAffinity(hKey, settings.nAssignmentSetOverride);
			settings.bHasPriority = ReadDword(hKey, AffinityPolicySubkey, DevicePriorityValue, settings.dwPriority);
			settings.bHasMSISupported = ReadDword(hKey, MSIPropertiesSubkey, MSISupportedValue, settings.dwMSISupported);
			settings.bHasMessageNumberLimit = ReadDword(hKey, MSIPropertiesSubkey, MessageNumberLimitValue, settings.dwMessageNumberLimit);
		}

		// writes the value when bHas, otherwise deletes it (a missing value or key is not an error)
		inline LSTATUS WriteOrDelete(HKEY hKey, const WCHAR* pwszSubkey, const WCHAR* pwszValue, bool bHas, DWORD dwType, const void* pData, DWORD cbData)
		{
			if (bHas)
			{
				HKEY hSubkey = nullptr;
				LSTATUS status = RegCreateKeyExW(hKey, pwszSubkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hSubkey, nullptr);
				if (ERROR_SUCCESS != status)
				{
					return status;
				}
				status = RegSetValueExW(hSubkey, pwszValue, 0, dwType, static_cast<const BYTE*>(pData), cbData);
				RegCloseKey(hSubkey);
				return status;
			}
			LSTATUS status = RegDeleteKeyValueW(hKey, pwszSubkey, pwszValue);
			return (ERROR_FILE_NOT_FOUND == status || ERROR_PATH_NOT_FOUND == status) ? ERROR_SUCCESS : status;
		}

		inline void ReadAllocatedInterrupts(DEVINST devInst, std::vector<AllocatedInterrupt>& vOut)
		{
			vOut.clear();
			LOG_CONF logConf = 0;
			if (CR_SUCCESS != CM_Get_First_Log_Conf(&logConf, devInst, ALLOC_LOG_CONF))
			{
				return;
			}
			RES_DES resDes = 0;
			RESOURCEID resId = 0;
			CONFIGRET cr = CM_Get_Next_Res_Des(&resDes, logConf, ResType_IRQ, &resId, 0);
			while (CR_SUCCESS == cr)
			{
				ULONG nSize = 0;
				if (CR_SUCCESS == CM_Get_Res_Des_Data_Size(&nSize, resDes, 0) && nSize >= sizeof(IrqDes64Grouped))
				{
					std::vector<BYTE> data(nSize);
					if (CR_SUCCESS == CM_Get_Res_Des_Data(resDes, data.data(), nSize, 0))
					{
						const IrqDes64Grouped* pDes = reinterpret_cast<const IrqDes64Grouped*>(data.data());
						AllocatedInterrupt irq;
						irq.nVector = pDes->IRQD_Alloc_Num;
						// line interrupts report 0xffffffff (not a real mask) when unrestricted
						irq.nAffinity = (0xffffffff == pDes->IRQD_Affinity) ? 0 : pDes->IRQD_Affinity;
						irq.bMessage = static_cast<LONG>(pDes->IRQD_Alloc_Num) < 0 || 0 != pDes->IRQD_Group;
						vOut.push_back(irq);
					}
				}
				RES_DES resDesNext = 0;
				cr = CM_Get_Next_Res_Des(&resDesNext, resDes, ResType_IRQ, &resId, 0);
				CM_Free_Res_Des_Handle(resDes);
				resDes = resDesNext;
			}
			CM_Free_Log_Conf_Handle(logConf);
		}

		// opens the device info set positioned on a single device instance
		class SingleDevice
		{
		public:
			HDEVINFO hDevInfo = INVALID_HANDLE_VALUE;
			SP_DEVINFO_DATA devInfo = { sizeof(SP_DEVINFO_DATA) };

			explicit SingleDevice(const std::wstring& strInstanceId)
			{
				hDevInfo = SetupDiCreateDeviceInfoList(nullptr, nullptr);
				if (INVALID_HANDLE_VALUE != hDevInfo && !SetupDiOpenDeviceInfoW(hDevInfo, strInstanceId.c_str(), nullptr, 0, &devInfo))
				{
					SetupDiDestroyDeviceInfoList(hDevInfo);
					hDevInfo = INVALID_HANDLE_VALUE;
				}
			}
			~SingleDevice()
			{
				if (INVALID_HANDLE_VALUE != hDevInfo)
				{
					SetupDiDestroyDeviceInfoList(hDevInfo);
				}
			}
			bool IsValid() const { return INVALID_HANDLE_VALUE != hDevInfo; }
			SingleDevice(const SingleDevice&) = delete;
			SingleDevice& operator=(const SingleDevice&) = delete;
		};
	}

	// enumerates present devices; unless bIncludeWithoutInterrupts, only those with allocated interrupts
	inline bool Enumerate(std::vector<Device>& vDevices, bool bIncludeWithoutInterrupts = false)
	{
		vDevices.clear();
		HDEVINFO hDevInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
		if (INVALID_HANDLE_VALUE == hDevInfo)
		{
			return false;
		}
		SP_DEVINFO_DATA devInfo = { sizeof(SP_DEVINFO_DATA) };
		for (DWORD nIndex = 0; SetupDiEnumDeviceInfo(hDevInfo, nIndex, &devInfo); nIndex++)
		{
			Device device;
			Detail::ReadAllocatedInterrupts(devInfo.DevInst, device.vInterrupts);
			if (device.vInterrupts.empty() && !bIncludeWithoutInterrupts)
			{
				continue;
			}
			WCHAR wszInstanceId[MAX_DEVICE_ID_LEN] = {};
			if (CR_SUCCESS != CM_Get_Device_IDW(devInfo.DevInst, wszInstanceId, _countof(wszInstanceId), 0))
			{
				continue;
			}
			// the HAL's interrupt reservation (hundreds of line IRQs), not a device with its own interrupts
			if (0 == _wcsnicmp(wszInstanceId, L"ACPI_HAL\\", 9) && !bIncludeWithoutInterrupts)
			{
				continue;
			}
			device.strInstanceId = wszInstanceId;
			device.strName = Detail::GetStringProperty(hDevInfo, devInfo, SPDRP_FRIENDLYNAME);
			if (device.strName.empty())
			{
				device.strName = Detail::GetStringProperty(hDevInfo, devInfo, SPDRP_DEVICEDESC);
			}
			device.strClass = Detail::GetStringProperty(hDevInfo, devInfo, SPDRP_CLASS);
			device.strLocation = Detail::GetStringProperty(hDevInfo, devInfo, SPDRP_LOCATION_INFORMATION);
			HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
			if (INVALID_HANDLE_VALUE != hKey)
			{
				Detail::ReadSettings(hKey, device.settings);
				device.bSettingsReadable = true;
				DWORD dwTargetGroup = 0;
				DWORD dwSize = sizeof(device.nTargetSet);
				if (Detail::ReadDword(hKey, Detail::TemporalPolicySubkey, L"TargetGroup", dwTargetGroup)
					&& ERROR_SUCCESS == RegGetValueW(hKey, Detail::TemporalPolicySubkey, L"TargetSet", RRF_RT_REG_QWORD, nullptr, &device.nTargetSet, &dwSize))
				{
					device.bHasTargetSet = true;
					device.nTargetGroup = static_cast<USHORT>(dwTargetGroup);
				}
				RegCloseKey(hKey);
			}
			vDevices.push_back(device);
		}
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return true;
	}

	inline bool GetSettings(const std::wstring& strInstanceId, Settings& settings)
	{
		Detail::SingleDevice dev(strInstanceId);
		if (!dev.IsValid())
		{
			return false;
		}
		HKEY hKey = SetupDiOpenDevRegKey(dev.hDevInfo, &dev.devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (INVALID_HANDLE_VALUE == hKey)
		{
			return false;
		}
		Detail::ReadSettings(hKey, settings);
		RegCloseKey(hKey);
		return true;
	}

	// writes every value present in settings and deletes every value absent from it; requires elevation
	inline bool SetSettings(const std::wstring& strInstanceId, const Settings& settings, LSTATUS* pStatus = nullptr)
	{
		LSTATUS status = ERROR_NOT_FOUND;
		Detail::SingleDevice dev(strInstanceId);
		if (dev.IsValid())
		{
			HKEY hKey = SetupDiOpenDevRegKey(dev.hDevInfo, &dev.devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ | KEY_WRITE);
			if (INVALID_HANDLE_VALUE == hKey)
			{
				status = static_cast<LSTATUS>(GetLastError());
			}
			else
			{
				using namespace Detail;
				const LSTATUS results[] =
				{
					WriteOrDelete(hKey, AffinityPolicySubkey, DevicePolicyValue, settings.bHasPolicy, REG_DWORD, &settings.dwPolicy, sizeof(DWORD)),
					WriteOrDelete(hKey, AffinityPolicySubkey, AssignmentSetOverrideValue, settings.bHasAssignmentSetOverride, REG_BINARY, &settings.nAssignmentSetOverride, sizeof(ULONGLONG)),
					WriteOrDelete(hKey, AffinityPolicySubkey, DevicePriorityValue, settings.bHasPriority, REG_DWORD, &settings.dwPriority, sizeof(DWORD)),
					WriteOrDelete(hKey, MSIPropertiesSubkey, MSISupportedValue, settings.bHasMSISupported, REG_DWORD, &settings.dwMSISupported, sizeof(DWORD)),
					WriteOrDelete(hKey, MSIPropertiesSubkey, MessageNumberLimitValue, settings.bHasMessageNumberLimit, REG_DWORD, &settings.dwMessageNumberLimit, sizeof(DWORD)),
				};
				status = ERROR_SUCCESS;
				for (LSTATUS result : results)
				{
					if (ERROR_SUCCESS != result)
					{
						status = result;
						break;
					}
				}
				RegCloseKey(hKey);
			}
		}
		if (pStatus)
		{
			*pStatus = status;
		}
		return ERROR_SUCCESS == status;
	}

	// classes whose restart can hang or blank a running system; these always use the reboot path
	inline bool IsRestartSafe(const Device& device)
	{
		static const WCHAR* const unsafeClasses[] = { L"DiskDrive", L"SCSIAdapter", L"HDC", L"Display", L"System", L"Computer", L"Processor", L"Volume", L"FloppyDisk", L"Keyboard" };
		for (const WCHAR* pwszClass : unsafeClasses)
		{
			if (0 == _wcsicmp(device.strClass.c_str(), pwszClass))
			{
				return false;
			}
		}
		Detail::SingleDevice dev(device.strInstanceId);
		if (!dev.IsValid())
		{
			return false;
		}
		ULONG nStatus = 0;
		ULONG nProblem = 0;
		if (CR_SUCCESS != CM_Get_DevNode_Status(&nStatus, &nProblem, dev.devInfo.DevInst, 0))
		{
			return false;
		}
		return 0 != (nStatus & DN_DISABLEABLE);
	}

	// restarts the device so new settings are read (same as disable + enable); sets bRebootRequired if setup reports it.
	// Must be called from a native (non-WOW64) process with elevation.
	inline bool RestartDevice(const std::wstring& strInstanceId, bool& bRebootRequired)
	{
		bRebootRequired = false;
		Detail::SingleDevice dev(strInstanceId);
		if (!dev.IsValid())
		{
			return false;
		}
		SP_PROPCHANGE_PARAMS params = {};
		params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
		params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
		params.StateChange = DICS_PROPCHANGE;
		params.Scope = DICS_FLAG_CONFIGSPECIFIC;
		params.HwProfile = 0;
		if (!SetupDiSetClassInstallParamsW(dev.hDevInfo, &dev.devInfo, &params.ClassInstallHeader, sizeof(params))
			|| !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, dev.hDevInfo, &dev.devInfo))
		{
			return false;
		}
		SP_DEVINSTALL_PARAMS_W installParams = { sizeof(SP_DEVINSTALL_PARAMS_W) };
		if (SetupDiGetDeviceInstallParamsW(dev.hDevInfo, &dev.devInfo, &installParams))
		{
			bRebootRequired = 0 != (installParams.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART));
		}
		return true;
	}

	inline const WCHAR* GetPolicyName(DWORD dwPolicy)
	{
		switch (dwPolicy)
		{
		case PolicyMachineDefault: return L"MachineDefault";
		case PolicyAllCloseProcessors: return L"AllCloseProcessors";
		case PolicyOneCloseProcessor: return L"OneCloseProcessor";
		case PolicyAllProcessorsInMachine: return L"AllProcessorsInMachine";
		case PolicySpecifiedProcessors: return L"SpecifiedProcessors";
		case PolicySpreadMessagesAcrossAllProcessors: return L"SpreadMessagesAcrossAllProcessors";
		case PolicyAllProcessorsInGroupWhenSteered: return L"AllProcessorsInGroupWhenSteered";
		default: return L"Unknown";
		}
	}
}
