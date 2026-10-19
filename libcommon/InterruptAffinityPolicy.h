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
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cwctype>

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
		// whether the Affinity Policy / MessageSignaledInterruptProperties subkeys exist; when false, SetSettings
		// removes the subkey once it holds no values, so a restore also removes keys created by an earlier change
		bool bAffinityPolicyKeyExists = false;
		bool bMSIKeyExists = false;

		// compares the values only (not whether empty subkeys exist)
		bool operator==(const Settings& o) const
		{
			return bHasPolicy == o.bHasPolicy && (!bHasPolicy || dwPolicy == o.dwPolicy)
				&& bHasAssignmentSetOverride == o.bHasAssignmentSetOverride && (!bHasAssignmentSetOverride || nAssignmentSetOverride == o.nAssignmentSetOverride)
				&& bHasPriority == o.bHasPriority && (!bHasPriority || dwPriority == o.dwPriority)
				&& bHasMSISupported == o.bHasMSISupported && (!bHasMSISupported || dwMSISupported == o.dwMSISupported)
				&& bHasMessageNumberLimit == o.bHasMessageNumberLimit && (!bHasMessageNumberLimit || dwMessageNumberLimit == o.dwMessageNumberLimit);
		}
		bool operator!=(const Settings& o) const { return !(*this == o); }
		// values and subkey existence
		bool IsIdentical(const Settings& o) const
		{
			return *this == o && bAffinityPolicyKeyExists == o.bAffinityPolicyKeyExists && bMSIKeyExists == o.bMSIKeyExists;
		}
		bool HasAnyValue() const
		{
			return bHasPolicy || bHasAssignmentSetOverride || bHasPriority || bHasMSISupported || bHasMessageNumberLimit;
		}
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

	// the exact registry form (type and bytes) of one value; backups store this so a restore is byte for byte
	struct RawValue
	{
		bool bPresent = false;
		DWORD dwType = REG_NONE;
		std::vector<BYTE> data;
		bool operator==(const RawValue& o) const { return bPresent == o.bPresent && (!bPresent || (dwType == o.dwType && data == o.data)); }
		bool operator!=(const RawValue& o) const { return !(*this == o); }
	};

	struct RawSnapshot
	{
		enum { DevicePolicy, AssignmentSetOverride, DevicePriority, MSISupported, MessageNumberLimit, ValueCount };
		RawValue values[ValueCount];
		bool bAffinityPolicyKeyExists = false;
		bool bMSIKeyExists = false;
		bool operator==(const RawSnapshot& o) const
		{
			for (size_t nI = 0; nI < ValueCount; nI++)
			{
				if (values[nI] != o.values[nI])
				{
					return false;
				}
			}
			return bAffinityPolicyKeyExists == o.bAffinityPolicyKeyExists && bMSIKeyExists == o.bMSIKeyExists;
		}
		bool operator!=(const RawSnapshot& o) const { return !(*this == o); }
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

		inline std::wstring Upper(std::wstring str)
		{
			std::transform(str.begin(), str.end(), str.begin(), [](wchar_t wc) { return static_cast<wchar_t>(std::towupper(wc)); });
			return str;
		}

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

		inline bool KeyExists(HKEY hKey, const WCHAR* pwszSubkey)
		{
			HKEY hSubkey = nullptr;
			if (ERROR_SUCCESS != RegOpenKeyExW(hKey, pwszSubkey, 0, KEY_QUERY_VALUE, &hSubkey))
			{
				return false;
			}
			RegCloseKey(hSubkey);
			return true;
		}

		// the five values, in RawSnapshot::values order
		const WCHAR* const SnapshotSubkeys[] = { AffinityPolicySubkey, AffinityPolicySubkey, AffinityPolicySubkey, MSIPropertiesSubkey, MSIPropertiesSubkey };
		const WCHAR* const SnapshotValueNames[] = { DevicePolicyValue, AssignmentSetOverrideValue, DevicePriorityValue, MSISupportedValue, MessageNumberLimitValue };

		inline bool ReadRawValue(HKEY hKey, const WCHAR* pwszSubkey, const WCHAR* pwszValue, RawValue& value)
		{
			value = RawValue();
			DWORD dwSize = 0;
			if (ERROR_SUCCESS != RegGetValueW(hKey, pwszSubkey, pwszValue, RRF_RT_ANY | RRF_NOEXPAND, &value.dwType, nullptr, &dwSize))
			{
				return false;
			}
			value.data.resize(dwSize);
			if (dwSize && ERROR_SUCCESS != RegGetValueW(hKey, pwszSubkey, pwszValue, RRF_RT_ANY | RRF_NOEXPAND, &value.dwType, value.data.data(), &dwSize))
			{
				value = RawValue();
				return false;
			}
			value.data.resize(dwSize);
			value.bPresent = true;
			return true;
		}

		inline void ReadRawSnapshot(HKEY hKey, RawSnapshot& snapshot)
		{
			snapshot = RawSnapshot();
			for (size_t nI = 0; nI < RawSnapshot::ValueCount; nI++)
			{
				ReadRawValue(hKey, SnapshotSubkeys[nI], SnapshotValueNames[nI], snapshot.values[nI]);
			}
			snapshot.bAffinityPolicyKeyExists = KeyExists(hKey, AffinityPolicySubkey);
			snapshot.bMSIKeyExists = KeyExists(hKey, MSIPropertiesSubkey);
		}

		inline bool ParseDword(const RawValue& value, DWORD& dwOut)
		{
			if (!value.bPresent || REG_DWORD != value.dwType || value.data.size() != sizeof(DWORD))
			{
				return false;
			}
			memcpy(&dwOut, value.data.data(), sizeof(DWORD));
			return true;
		}

		// KAFFINITY as REG_BINARY (INFs often write 1 byte) or REG_QWORD, up to 8 bytes
		inline bool ParseAffinity(const RawValue& value, ULONGLONG& nOut)
		{
			if (!value.bPresent || (REG_BINARY != value.dwType && REG_QWORD != value.dwType) || value.data.empty() || value.data.size() > sizeof(ULONGLONG))
			{
				return false;
			}
			nOut = 0;
			memcpy(&nOut, value.data.data(), value.data.size());
			return true;
		}
	}

	// the typed view of a snapshot; a value stored in an unexpected type reads as absent (and SetSettings leaves it alone)
	inline Settings SettingsFromSnapshot(const RawSnapshot& snapshot)
	{
		using namespace Detail;
		Settings settings;
		settings.bHasPolicy = ParseDword(snapshot.values[RawSnapshot::DevicePolicy], settings.dwPolicy);
		settings.bHasAssignmentSetOverride = ParseAffinity(snapshot.values[RawSnapshot::AssignmentSetOverride], settings.nAssignmentSetOverride);
		settings.bHasPriority = ParseDword(snapshot.values[RawSnapshot::DevicePriority], settings.dwPriority);
		settings.bHasMSISupported = ParseDword(snapshot.values[RawSnapshot::MSISupported], settings.dwMSISupported);
		settings.bHasMessageNumberLimit = ParseDword(snapshot.values[RawSnapshot::MessageNumberLimit], settings.dwMessageNumberLimit);
		settings.bAffinityPolicyKeyExists = snapshot.bAffinityPolicyKeyExists;
		settings.bMSIKeyExists = snapshot.bMSIKeyExists;
		return settings;
	}

	namespace Detail
	{
		inline void ReadSettings(HKEY hKey, Settings& settings)
		{
			RawSnapshot snapshot;
			ReadRawSnapshot(hKey, snapshot);
			settings = SettingsFromSnapshot(snapshot);
		}

		// deletes the subkey if it has no values and no subkeys (a missing key is not an error)
		inline LSTATUS DeleteKeyIfEmpty(HKEY hKey, const WCHAR* pwszSubkey)
		{
			HKEY hSubkey = nullptr;
			LSTATUS status = RegOpenKeyExW(hKey, pwszSubkey, 0, KEY_QUERY_VALUE, &hSubkey);
			if (ERROR_FILE_NOT_FOUND == status || ERROR_PATH_NOT_FOUND == status)
			{
				return ERROR_SUCCESS;
			}
			if (ERROR_SUCCESS != status)
			{
				return status;
			}
			DWORD dwSubkeys = 0;
			DWORD dwValues = 0;
			status = RegQueryInfoKeyW(hSubkey, nullptr, nullptr, nullptr, &dwSubkeys, nullptr, nullptr, &dwValues, nullptr, nullptr, nullptr, nullptr);
			RegCloseKey(hSubkey);
			if (ERROR_SUCCESS != status || dwSubkeys || dwValues)
			{
				return status;
			}
			return RegDeleteKeyW(hKey, pwszSubkey);
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

	inline bool GetSnapshot(const std::wstring& strInstanceId, RawSnapshot& snapshot)
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
		Detail::ReadRawSnapshot(hKey, snapshot);
		RegCloseKey(hKey);
		return true;
	}

	namespace Detail
	{
		inline LSTATUS FirstError(std::initializer_list<LSTATUS> results)
		{
			for (LSTATUS result : results)
			{
				if (ERROR_SUCCESS != result)
				{
					return result;
				}
			}
			return ERROR_SUCCESS;
		}

		// opens the device's hardware key for writing and runs fnWrite(hKey); requires elevation
		template <typename FN> inline bool WriteDeviceKey(const std::wstring& strInstanceId, LSTATUS* pStatus, FN fnWrite)
		{
			LSTATUS status = ERROR_NOT_FOUND;
			SingleDevice dev(strInstanceId);
			if (dev.IsValid())
			{
				HKEY hKey = SetupDiOpenDevRegKey(dev.hDevInfo, &dev.devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ | KEY_WRITE);
				if (INVALID_HANDLE_VALUE == hKey)
				{
					status = static_cast<LSTATUS>(GetLastError());
				}
				else
				{
					status = fnWrite(hKey);
					RegCloseKey(hKey);
				}
			}
			if (pStatus)
			{
				*pStatus = status;
			}
			return ERROR_SUCCESS == status;
		}
	}

	// writes a snapshot exactly: each value with its original type and bytes, absent values deleted, and subkeys that
	// did not exist removed once empty
	inline bool SetSnapshot(const std::wstring& strInstanceId, const RawSnapshot& snapshot, LSTATUS* pStatus = nullptr)
	{
		return Detail::WriteDeviceKey(strInstanceId, pStatus, [&snapshot](HKEY hKey)
			{
				using namespace Detail;
				for (size_t nI = 0; nI < RawSnapshot::ValueCount; nI++)
				{
					const RawValue& value = snapshot.values[nI];
					const LSTATUS status = WriteOrDelete(hKey, SnapshotSubkeys[nI], SnapshotValueNames[nI], value.bPresent, value.dwType,
						value.data.data(), static_cast<DWORD>(value.data.size()));
					if (ERROR_SUCCESS != status)
					{
						return status;
					}
				}
				return FirstError({
					snapshot.bAffinityPolicyKeyExists ? ERROR_SUCCESS : DeleteKeyIfEmpty(hKey, AffinityPolicySubkey),
					snapshot.bMSIKeyExists ? ERROR_SUCCESS : DeleteKeyIfEmpty(hKey, MSIPropertiesSubkey) });
			});
	}

	// writes the values whose meaning differs from the device's current ones (new values as REG_DWORD, and the mask as
	// an 8 byte REG_BINARY) and deletes those now absent. Values that do not change, including ones in an unexpected
	// type, are left byte for byte as they are. Requires elevation.
	inline bool SetSettings(const std::wstring& strInstanceId, const Settings& settings, LSTATUS* pStatus = nullptr)
	{
		RawSnapshot current;
		if (!GetSnapshot(strInstanceId, current))
		{
			if (pStatus)
			{
				*pStatus = ERROR_NOT_FOUND;
			}
			return false;
		}
		const Settings now = SettingsFromSnapshot(current);
		return Detail::WriteDeviceKey(strInstanceId, pStatus, [&settings, &now](HKEY hKey)
			{
				using namespace Detail;
				auto writeDword = [hKey](bool bChanged, const WCHAR* pwszSubkey, const WCHAR* pwszValue, bool bHas, const DWORD& dw)
				{
					return bChanged ? WriteOrDelete(hKey, pwszSubkey, pwszValue, bHas, REG_DWORD, &dw, sizeof(DWORD)) : ERROR_SUCCESS;
				};
				const auto changed = [](bool bHasA, ULONGLONG a, bool bHasB, ULONGLONG b) { return bHasA != bHasB || (bHasA && a != b); };
				return FirstError({
					writeDword(changed(settings.bHasPolicy, settings.dwPolicy, now.bHasPolicy, now.dwPolicy), AffinityPolicySubkey, DevicePolicyValue, settings.bHasPolicy, settings.dwPolicy),
					changed(settings.bHasAssignmentSetOverride, settings.nAssignmentSetOverride, now.bHasAssignmentSetOverride, now.nAssignmentSetOverride)
						? WriteOrDelete(hKey, AffinityPolicySubkey, AssignmentSetOverrideValue, settings.bHasAssignmentSetOverride, REG_BINARY, &settings.nAssignmentSetOverride, sizeof(ULONGLONG)) : ERROR_SUCCESS,
					writeDword(changed(settings.bHasPriority, settings.dwPriority, now.bHasPriority, now.dwPriority), AffinityPolicySubkey, DevicePriorityValue, settings.bHasPriority, settings.dwPriority),
					writeDword(changed(settings.bHasMSISupported, settings.dwMSISupported, now.bHasMSISupported, now.dwMSISupported), MSIPropertiesSubkey, MSISupportedValue, settings.bHasMSISupported, settings.dwMSISupported),
					writeDword(changed(settings.bHasMessageNumberLimit, settings.dwMessageNumberLimit, now.bHasMessageNumberLimit, now.dwMessageNumberLimit), MSIPropertiesSubkey, MessageNumberLimitValue, settings.bHasMessageNumberLimit, settings.dwMessageNumberLimit),
					settings.bAffinityPolicyKeyExists ? ERROR_SUCCESS : DeleteKeyIfEmpty(hKey, AffinityPolicySubkey),
					settings.bMSIKeyExists ? ERROR_SUCCESS : DeleteKeyIfEmpty(hKey, MSIPropertiesSubkey) });
			});
	}

	//
	// restart safety: whether a device can be restarted from the running system to apply new settings, or needs a reboot
	//

	enum RestartSafety
	{
		RestartSafe,
		RestartUnsafeClass,			// storage, display, processor, PS/2 input and similar classes
		RestartUnsafeSystemPath,	// on the device path of the Windows, paging file or display devices
		RestartUnsafeBridge,		// a PCI bridge or root port; a restart cascades to everything behind it
		RestartUnsafePlatform,		// a non-PCI system device (ACPI, HAL)
		RestartUnsafeNotDisableable,
	};

	struct RestartAssessment
	{
		RestartSafety safety = RestartUnsafeNotDisableable;
		bool bInterruptsInput = false;		// USB input stops for a moment
		bool bInterruptsNetwork = false;	// network connections drop for a moment
		bool IsSafe() const { return RestartSafe == safety; }
	};

	namespace Detail
	{
		// GUID_DEVINTERFACE_DISK, declared locally so no translation unit needs INITGUID
		const GUID DiskInterfaceGuid = { 0x53f56307, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };

		inline std::wstring GetInstanceId(DEVINST devInst)
		{
			WCHAR wszInstanceId[MAX_DEVICE_ID_LEN] = {};
			return CR_SUCCESS == CM_Get_Device_IDW(devInst, wszInstanceId, _countof(wszInstanceId), 0) ? std::wstring(wszInstanceId) : std::wstring();
		}

		inline void AddWithAncestors(DEVINST devInst, std::set<std::wstring>& setOut)
		{
			for (int nDepth = 0; nDepth < 64; nDepth++)
			{
				const std::wstring strInstanceId = GetInstanceId(devInst);
				if (!strInstanceId.empty())
				{
					setOut.insert(Upper(strInstanceId));
				}
				DEVINST devParent = 0;
				if (CR_SUCCESS != CM_Get_Parent(&devParent, devInst, 0))
				{
					break;
				}
				devInst = devParent;
			}
		}

		// physical disk numbers backing the volume with this drive letter
		inline void AddDiskNumbersOfVolume(WCHAR wcDrive, std::set<DWORD>& setOut)
		{
			const WCHAR wszVolume[] = { L'\\', L'\\', L'.', L'\\', wcDrive, L':', 0 };
			HANDLE hVolume = CreateFileW(wszVolume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (INVALID_HANDLE_VALUE == hVolume)
			{
				return;
			}
			std::vector<BYTE> buffer(sizeof(VOLUME_DISK_EXTENTS) + 31 * sizeof(DISK_EXTENT));
			DWORD dwReturned = 0;
			if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size()), &dwReturned, nullptr))
			{
				const VOLUME_DISK_EXTENTS* pExtents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
				for (DWORD nI = 0; nI < pExtents->NumberOfDiskExtents && nI < 32; nI++)
				{
					setOut.insert(pExtents->Extents[nI].DiskNumber);
				}
			}
			CloseHandle(hVolume);
		}
	}

	// instance ids (upper case) of the devices that must stay running: the disks holding Windows and the paging files,
	// display adapters, and every ancestor of those. Build once per dialog session and pass to AssessRestart.
	inline std::set<std::wstring> GetSystemCriticalDevices()
	{
		std::set<std::wstring> setCritical;

		// drive letters of the Windows volume and of the existing paging files ("\??\C:\pagefile.sys")
		std::set<WCHAR> setDrives;
		WCHAR wszWindows[MAX_PATH] = {};
		if (GetSystemWindowsDirectoryW(wszWindows, _countof(wszWindows)) && wszWindows[0] && L':' == wszWindows[1])
		{
			setDrives.insert(towupper(wszWindows[0]));
		}
		WCHAR wszPagingFiles[2048] = {};
		DWORD dwSize = sizeof(wszPagingFiles) - 2 * sizeof(WCHAR);
		if (ERROR_SUCCESS == RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", L"ExistingPageFiles", RRF_RT_REG_MULTI_SZ, nullptr, wszPagingFiles, &dwSize))
		{
			for (const WCHAR* pwsz = wszPagingFiles; *pwsz; pwsz += wcslen(pwsz) + 1)
			{
				const WCHAR* pwszColon = wcschr(pwsz, L':');
				if (pwszColon && pwszColon > pwsz && iswalpha(pwszColon[-1]))
				{
					setDrives.insert(towupper(pwszColon[-1]));
				}
			}
		}
		std::set<DWORD> setDiskNumbers;
		for (WCHAR wcDrive : setDrives)
		{
			Detail::AddDiskNumbersOfVolume(wcDrive, setDiskNumbers);
		}

		// map disk numbers to disk devnodes through the disk interfaces
		HDEVINFO hDisks = SetupDiGetClassDevsW(&Detail::DiskInterfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (INVALID_HANDLE_VALUE != hDisks)
		{
			SP_DEVICE_INTERFACE_DATA interfaceData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
			for (DWORD nIndex = 0; SetupDiEnumDeviceInterfaces(hDisks, nullptr, &Detail::DiskInterfaceGuid, nIndex, &interfaceData); nIndex++)
			{
				DWORD dwRequired = 0;
				SetupDiGetDeviceInterfaceDetailW(hDisks, &interfaceData, nullptr, 0, &dwRequired, nullptr);
				if (dwRequired < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
				{
					continue;
				}
				std::vector<BYTE> detailBuffer(dwRequired);
				SP_DEVICE_INTERFACE_DETAIL_DATA_W* pDetail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
				pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
				SP_DEVINFO_DATA devInfo = { sizeof(SP_DEVINFO_DATA) };
				if (!SetupDiGetDeviceInterfaceDetailW(hDisks, &interfaceData, pDetail, dwRequired, nullptr, &devInfo))
				{
					continue;
				}
				HANDLE hDisk = CreateFileW(pDetail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
				if (INVALID_HANDLE_VALUE == hDisk)
				{
					continue;
				}
				STORAGE_DEVICE_NUMBER deviceNumber = {};
				DWORD dwReturned = 0;
				if (DeviceIoControl(hDisk, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &deviceNumber, sizeof(deviceNumber), &dwReturned, nullptr)
					&& setDiskNumbers.count(deviceNumber.DeviceNumber))
				{
					Detail::AddWithAncestors(devInfo.DevInst, setCritical);
				}
				CloseHandle(hDisk);
			}
			SetupDiDestroyDeviceInfoList(hDisks);
		}

		// display adapters and their bridges
		HDEVINFO hAll = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
		if (INVALID_HANDLE_VALUE != hAll)
		{
			SP_DEVINFO_DATA devInfo = { sizeof(SP_DEVINFO_DATA) };
			for (DWORD nIndex = 0; SetupDiEnumDeviceInfo(hAll, nIndex, &devInfo); nIndex++)
			{
				if (0 == _wcsicmp(Detail::GetStringProperty(hAll, devInfo, SPDRP_CLASS).c_str(), L"Display"))
				{
					Detail::AddWithAncestors(devInfo.DevInst, setCritical);
				}
			}
			SetupDiDestroyDeviceInfoList(hAll);
		}
		return setCritical;
	}

	inline RestartAssessment AssessRestart(const Device& device, const std::set<std::wstring>& setCritical)
	{
		RestartAssessment assessment;
		const WCHAR* const pwszClass = device.strClass.c_str();
		assessment.bInterruptsInput = 0 == _wcsicmp(pwszClass, L"USB") || 0 == _wcsicmp(pwszClass, L"HIDClass");
		assessment.bInterruptsNetwork = 0 == _wcsicmp(pwszClass, L"Net");

		static const WCHAR* const unsafeClasses[] = { L"DiskDrive", L"SCSIAdapter", L"HDC", L"Display", L"Computer", L"Processor", L"Volume", L"FloppyDisk", L"Keyboard", L"Mouse" };
		for (const WCHAR* pwszUnsafe : unsafeClasses)
		{
			if (0 == _wcsicmp(pwszClass, pwszUnsafe))
			{
				assessment.safety = RestartUnsafeClass;
				return assessment;
			}
		}
		if (setCritical.count(Detail::Upper(device.strInstanceId)))
		{
			assessment.safety = RestartUnsafeSystemPath;
			return assessment;
		}
		const bool bPCI = 0 == _wcsnicmp(device.strInstanceId.c_str(), L"PCI\\", 4);
		if (!bPCI && 0 == _wcsicmp(pwszClass, L"System"))
		{
			assessment.safety = RestartUnsafePlatform;
			return assessment;
		}

		Detail::SingleDevice dev(device.strInstanceId);
		if (!dev.IsValid())
		{
			return assessment;
		}
		if (bPCI)
		{
			// PCI base class 06 is bridges (host, ISA, PCI-to-PCI / root ports, ...)
			WCHAR wszCompatibleIds[2048] = {};
			if (SetupDiGetDeviceRegistryPropertyW(dev.hDevInfo, &dev.devInfo, SPDRP_COMPATIBLEIDS, nullptr, reinterpret_cast<PBYTE>(wszCompatibleIds), sizeof(wszCompatibleIds) - 2 * sizeof(WCHAR), nullptr))
			{
				for (const WCHAR* pwsz = wszCompatibleIds; *pwsz; pwsz += wcslen(pwsz) + 1)
				{
					if (0 == _wcsnicmp(pwsz, L"PCI\\CC_06", 9))
					{
						assessment.safety = RestartUnsafeBridge;
						return assessment;
					}
				}
			}
		}
		ULONG nStatus = 0;
		ULONG nProblem = 0;
		if (CR_SUCCESS != CM_Get_DevNode_Status(&nStatus, &nProblem, dev.devInfo.DevInst, 0) || !(nStatus & DN_DISABLEABLE))
		{
			return assessment;
		}
		assessment.safety = RestartSafe;
		return assessment;
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

	// waits for a restarted device to start; returns false with the CM problem code if it failed to start
	inline bool WaitForDeviceStart(const std::wstring& strInstanceId, ULONG& nProblem, DWORD dwTimeoutMs = 10000)
	{
		nProblem = 0;
		const ULONGLONG nDeadline = GetTickCount64() + dwTimeoutMs;
		for (;;)
		{
			DEVINST devInst = 0;
			ULONG nStatus = 0;
			if (CR_SUCCESS == CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(strInstanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL)
				&& CR_SUCCESS == CM_Get_DevNode_Status(&nStatus, &nProblem, devInst, 0))
			{
				if ((nStatus & DN_STARTED) && !(nStatus & DN_HAS_PROBLEM))
				{
					nProblem = 0;
					return true;
				}
				if (!(nStatus & DN_HAS_PROBLEM))
				{
					nProblem = 0;
				}
			}
			if (GetTickCount64() >= nDeadline)
			{
				return false;
			}
			Sleep(100);
		}
	}

	// Original settings of each device changed by the product, so they can be restored (dialog, Reset Configuration, uninstall).
	// One subkey per device under HKLM\<strRootKey>, named by the instance id with '\' replaced by '#'. Each subkey holds the
	// InstanceId, the policy / MSI values that existed with their original types and bytes, and whether their subkeys existed.
	// Requires elevation to write.
	class BackupStore
	{
		std::wstring m_strRootKey;

		static std::wstring EscapeInstanceId(std::wstring str)
		{
			std::replace(str.begin(), str.end(), L'\\', L'#');
			return str;
		}
		std::wstring GetDeviceKey(const std::wstring& strInstanceId) const
		{
			return m_strRootKey + L"\\" + EscapeInstanceId(strInstanceId);
		}
	public:
		explicit BackupStore(const WCHAR* pwszRootKey) : m_strRootKey(pwszRootKey) {}

		bool Has(const std::wstring& strInstanceId) const
		{
			HKEY hKey = nullptr;
			if (ERROR_SUCCESS != RegOpenKeyExW(HKEY_LOCAL_MACHINE, GetDeviceKey(strInstanceId).c_str(), 0, KEY_QUERY_VALUE, &hKey))
			{
				return false;
			}
			RegCloseKey(hKey);
			return true;
		}

		// stores the device's values exactly as they are (type and bytes); absent values are simply not stored
		bool Save(const std::wstring& strInstanceId, const RawSnapshot& original)
		{
			HKEY hKey = nullptr;
			if (ERROR_SUCCESS != RegCreateKeyExW(HKEY_LOCAL_MACHINE, GetDeviceKey(strInstanceId).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &hKey, nullptr))
			{
				return false;
			}
			using namespace Detail;
			const DWORD dwPolicyKeyExisted = original.bAffinityPolicyKeyExists ? 1 : 0;
			const DWORD dwMSIKeyExisted = original.bMSIKeyExists ? 1 : 0;
			LSTATUS status = FirstError({
				RegSetValueExW(hKey, L"InstanceId", 0, REG_SZ, reinterpret_cast<const BYTE*>(strInstanceId.c_str()), static_cast<DWORD>((strInstanceId.size() + 1) * sizeof(WCHAR))),
				RegSetValueExW(hKey, L"AffinityPolicyKeyExisted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwPolicyKeyExisted), sizeof(DWORD)),
				RegSetValueExW(hKey, L"MSIKeyExisted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwMSIKeyExisted), sizeof(DWORD)) });
			for (size_t nI = 0; nI < RawSnapshot::ValueCount && ERROR_SUCCESS == status; nI++)
			{
				const RawValue& value = original.values[nI];
				status = WriteOrDelete(hKey, L"", SnapshotValueNames[nI], value.bPresent, value.dwType, value.data.data(), static_cast<DWORD>(value.data.size()));
			}
			RegCloseKey(hKey);
			if (ERROR_SUCCESS != status)
			{
				Remove(strInstanceId);	// never leave a partial backup; it would restore the wrong values
				return false;
			}
			return true;
		}

		bool Load(const std::wstring& strInstanceId, RawSnapshot& original) const
		{
			HKEY hKey = nullptr;
			if (ERROR_SUCCESS != RegOpenKeyExW(HKEY_LOCAL_MACHINE, GetDeviceKey(strInstanceId).c_str(), 0, KEY_QUERY_VALUE, &hKey))
			{
				return false;
			}
			using namespace Detail;
			original = RawSnapshot();
			DWORD dwExisted = 0;
			original.bAffinityPolicyKeyExists = ReadDword(hKey, nullptr, L"AffinityPolicyKeyExisted", dwExisted) && dwExisted;
			original.bMSIKeyExists = ReadDword(hKey, nullptr, L"MSIKeyExisted", dwExisted) && dwExisted;
			for (size_t nI = 0; nI < RawSnapshot::ValueCount; nI++)
			{
				ReadRawValue(hKey, nullptr, SnapshotValueNames[nI], original.values[nI]);
			}
			RegCloseKey(hKey);
			return true;
		}

		// the typed view of the saved original
		bool Load(const std::wstring& strInstanceId, Settings& original) const
		{
			RawSnapshot snapshot;
			if (!Load(strInstanceId, snapshot))
			{
				return false;
			}
			original = SettingsFromSnapshot(snapshot);
			return true;
		}

		bool Remove(const std::wstring& strInstanceId)
		{
			const LSTATUS status = RegDeleteKeyW(HKEY_LOCAL_MACHINE, GetDeviceKey(strInstanceId).c_str());
			if (ERROR_SUCCESS == status)
			{
				HKEY hRoot = nullptr;	// drop the root once the last device is restored
				if (ERROR_SUCCESS == RegOpenKeyExW(HKEY_LOCAL_MACHINE, m_strRootKey.c_str(), 0, KEY_QUERY_VALUE, &hRoot))
				{
					DWORD dwSubkeys = 1;
					RegQueryInfoKeyW(hRoot, nullptr, nullptr, nullptr, &dwSubkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
					RegCloseKey(hRoot);
					if (!dwSubkeys)
					{
						RegDeleteKeyW(HKEY_LOCAL_MACHINE, m_strRootKey.c_str());
					}
				}
			}
			return ERROR_SUCCESS == status || ERROR_FILE_NOT_FOUND == status;
		}

		// instance ids of every device with a backup
		std::vector<std::wstring> List() const
		{
			std::vector<std::wstring> vInstanceIds;
			HKEY hRoot = nullptr;
			if (ERROR_SUCCESS != RegOpenKeyExW(HKEY_LOCAL_MACHINE, m_strRootKey.c_str(), 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hRoot))
			{
				return vInstanceIds;
			}
			WCHAR wszSubkey[MAX_DEVICE_ID_LEN + 1];
			for (DWORD nIndex = 0;; nIndex++)
			{
				DWORD cchSubkey = _countof(wszSubkey);
				if (ERROR_SUCCESS != RegEnumKeyExW(hRoot, nIndex, wszSubkey, &cchSubkey, nullptr, nullptr, nullptr, nullptr))
				{
					break;
				}
				WCHAR wszInstanceId[MAX_DEVICE_ID_LEN + 1] = {};
				DWORD dwSize = sizeof(wszInstanceId) - sizeof(WCHAR);
				if (ERROR_SUCCESS == RegGetValueW(hRoot, wszSubkey, L"InstanceId", RRF_RT_REG_SZ, nullptr, wszInstanceId, &dwSize))
				{
					vInstanceIds.push_back(wszInstanceId);
				}
			}
			RegCloseKey(hRoot);
			return vInstanceIds;
		}
	};

	//
	// apply / restore, with backup and optional restart
	//

	enum ApplyStatus
	{
		ApplyFailed,				// nothing changed (registry write or backup failed)
		AppliedUnchanged,			// the device already had these settings
		AppliedRestarted,			// device restarted and started with the new settings
		AppliedRebootRequired,		// written; applies at the next reboot (no restart requested, restart failed, or setup asked for a reboot)
		AppliedRolledBack,			// the device failed to start with the new settings; the previous settings were put back and it was restarted again
		AppliedRollbackFailed,		// the device failed to start and putting back the previous settings failed too; restore and reboot
	};

	struct ApplyOutcome
	{
		ApplyStatus status = ApplyFailed;
		LSTATUS nError = ERROR_SUCCESS;
		ULONG nProblem = 0;		// CM problem code when the device failed to start
	};

	namespace Detail
	{
		inline void RestartAndVerify(const std::wstring& strInstanceId, const RawSnapshot& previous, ApplyOutcome& outcome)
		{
			bool bRebootRequired = false;
			if (!RestartDevice(strInstanceId, bRebootRequired) || bRebootRequired)
			{
				outcome.status = AppliedRebootRequired;
				return;
			}
			if (WaitForDeviceStart(strInstanceId, outcome.nProblem))
			{
				outcome.status = AppliedRestarted;
				return;
			}
			if (!outcome.nProblem)
			{
				// still starting without a problem code; don't undo the change, a reboot settles it either way
				outcome.status = AppliedRebootRequired;
				return;
			}
			// failed to start (e.g. MSI turned on for a driver without MSI support): put the previous values back exactly
			if (!SetSnapshot(strInstanceId, previous, &outcome.nError))
			{
				outcome.status = AppliedRollbackFailed;
				return;
			}
			ULONG nProblemAfterRollback = 0;
			outcome.status = (RestartDevice(strInstanceId, bRebootRequired) && !bRebootRequired && WaitForDeviceStart(strInstanceId, nProblemAfterRollback))
				? AppliedRolledBack : AppliedRollbackFailed;
		}
	}

	// writes new settings for a device, first saving its exact original values unless a backup already exists (the
	// first change keeps the true original). If the new settings mean the same as the original, the original is written
	// back byte for byte (removing subkeys the change created) and the backup is dropped. bRestart restarts the device
	// to apply them; only pass it when AssessRestart says the device is safe to restart.
	inline ApplyOutcome Apply(BackupStore& store, const std::wstring& strInstanceId, const Settings& updated, bool bRestart)
	{
		ApplyOutcome outcome;
		RawSnapshot previous;
		if (!GetSnapshot(strInstanceId, previous))
		{
			outcome.nError = ERROR_NOT_FOUND;
			return outcome;
		}
		const Settings previousSettings = SettingsFromSnapshot(previous);
		RawSnapshot original;
		bool bNewBackup = false;
		if (!store.Load(strInstanceId, original))
		{
			if (updated == previousSettings)
			{
				outcome.status = AppliedUnchanged;
				return outcome;
			}
			original = previous;
			if (!store.Save(strInstanceId, original))
			{
				outcome.nError = ERROR_WRITE_FAULT;
				return outcome;
			}
			bNewBackup = true;
		}
		const bool bBackToOriginal = (updated == SettingsFromSnapshot(original));
		if (bBackToOriginal ? (previous == original) : (updated == previousSettings))
		{
			if (bBackToOriginal)
			{
				store.Remove(strInstanceId);
			}
			outcome.status = AppliedUnchanged;
			return outcome;
		}
		const bool bWritten = bBackToOriginal ? SetSnapshot(strInstanceId, original, &outcome.nError) : SetSettings(strInstanceId, updated, &outcome.nError);
		if (!bWritten)
		{
			SetSnapshot(strInstanceId, previous);
			if (bNewBackup)
			{
				store.Remove(strInstanceId);
			}
			return outcome;
		}
		if (bBackToOriginal)
		{
			store.Remove(strInstanceId);
		}
		if (!bRestart)
		{
			outcome.status = AppliedRebootRequired;
			return outcome;
		}
		Detail::RestartAndVerify(strInstanceId, previous, outcome);
		return outcome;
	}

	// writes the saved original values back exactly and drops the backup
	inline ApplyOutcome Restore(BackupStore& store, const std::wstring& strInstanceId, bool bRestart)
	{
		ApplyOutcome outcome;
		RawSnapshot original;
		RawSnapshot previous;
		if (!store.Load(strInstanceId, original))
		{
			outcome.nError = ERROR_NOT_FOUND;
			return outcome;
		}
		if (!GetSnapshot(strInstanceId, previous))
		{
			// device is gone from the system; nothing to restore
			store.Remove(strInstanceId);
			outcome.nError = ERROR_NOT_FOUND;
			return outcome;
		}
		if (!SetSnapshot(strInstanceId, original, &outcome.nError))
		{
			return outcome;
		}
		store.Remove(strInstanceId);
		if (previous == original)
		{
			outcome.status = AppliedUnchanged;
			return outcome;
		}
		if (!bRestart)
		{
			outcome.status = AppliedRebootRequired;
			return outcome;
		}
		Detail::RestartAndVerify(strInstanceId, previous, outcome);
		return outcome;
	}

	// restores every device with a backup without restarting any (for uninstall and Reset Configuration);
	// returns true if anything changed, which needs a reboot. Failed instance ids go to pvFailed.
	inline bool RestoreAll(BackupStore& store, std::vector<std::wstring>* pvFailed = nullptr)
	{
		bool bChanged = false;
		for (const auto& strInstanceId : store.List())
		{
			Settings previous;
			const bool bPresent = GetSettings(strInstanceId, previous);
			const ApplyOutcome outcome = Restore(store, strInstanceId, false);
			if (AppliedRebootRequired == outcome.status)
			{
				bChanged = true;
			}
			else if (ApplyFailed == outcome.status && bPresent && pvFailed)
			{
				pvFailed->push_back(strInstanceId);
			}
		}
		return bChanged;
	}

	// The backup store every Bitsum program with the interrupt tool shares (Process Lasso, LatencyTest, IRQControl), so each
	// shows and restores the changes made in any of them. Until 2026-10 it was under Process Lasso's key (LegacyBackupRootKey),
	// which Process Lasso's uninstaller deletes.
	const WCHAR* const SharedBackupRootKey = L"Software\\Bitsum\\InterruptAffinityBackup";
	const WCHAR* const LegacyBackupRootKey = L"Software\\ProcessLasso\\InterruptAffinityBackup";
	// held while an interrupt settings editor is open in any of those programs, so only one edits the shared backups at a time
	const WCHAR* const EditorMutexName = L"Global\\BitsumInterruptAffinityEditor";

	// moves the backups under pwszFromRootKey into pwszToRootKey, byte for byte. The first change wins: a device already in
	// the destination keeps that backup, and the old one is dropped. A backup that fails to copy stays where it was.
	inline void MigrateBackups(const WCHAR* pwszFromRootKey, const WCHAR* pwszToRootKey)
	{
		BackupStore from(pwszFromRootKey);
		BackupStore to(pwszToRootKey);
		for (const auto& strInstanceId : from.List())
		{
			RawSnapshot original;
			if (!to.Has(strInstanceId))
			{
				if (!from.Load(strInstanceId, original) || !to.Save(strInstanceId, original))
				{
					continue;
				}
			}
			from.Remove(strInstanceId);
		}
	}
}
