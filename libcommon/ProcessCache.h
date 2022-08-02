#pragma once
// ProcessCache
//  encapsulates associative arrays of various types for storage of process stats
//
// TODO: 
//  template this if continued use (each datatype will have an instance)
//  replace get/set methods referencing specifically named values with generic get/set_byName methods

#include <unordered_map>
#include <mutex>

class ProcessCache
{
public:
	enum valueName {
		CacheValThreadCount,
		CacheValIODelta,
		CacheRunningState,
		CacheCPUTimeTotal
	};
private:
	std::mutex prot;
	std::unordered_map<unsigned int, double> mapCPUUse;
	std::unordered_map<unsigned int, double> mapAverageCPU;
	std::unordered_map<unsigned int, unsigned __int64> mapPrivateWorkingSet;
	std::unordered_map<unsigned int, std::unordered_map<valueName, unsigned long>> mapNamedULONGs;
	std::unordered_map<unsigned int, std::unordered_map<valueName, unsigned __int64>> mapNamedULONGLONGs;
public:
	void erase(const unsigned int pid)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapCPUUse.erase(pid);
		mapAverageCPU.erase(pid);
		mapPrivateWorkingSet.erase(pid);
		if (mapNamedULONGLONGs.find(pid) != mapNamedULONGLONGs.end())
		{
			mapNamedULONGLONGs[pid].clear();
		}
		mapNamedULONGLONGs.erase(pid);
		if (mapNamedULONGs.find(pid) != mapNamedULONGs.end())
		{
			mapNamedULONGs[pid].clear();
		}
		mapNamedULONGs.erase(pid);
		// safety check for map leakage
		_ASSERT(mapCPUUse.size() < 4096 && mapAverageCPU.size() < 4096 && mapPrivateWorkingSet.size() < 4096
			&& mapNamedULONGs.size() < 4096 && mapNamedULONGLONGs.size() < 4096);
	}

	void set_byName(const unsigned int pid, const valueName valName, const unsigned long nVal)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapNamedULONGs[pid][valName] = nVal;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapNamedULONGs.size() < 1000 && mapNamedULONGs[pid].size() < 1000);
	}
	bool get_byName(const unsigned int pid, const valueName valName, unsigned long& nVal)
	{
		std::lock_guard<std::mutex> lock(prot);
		auto& i = mapNamedULONGs.find(pid);
		if (i != mapNamedULONGs.end())
		{
			auto i2 = i->second.find(valName);
			if (i2 != i->second.end())
			{
				nVal = i2->second;
				return true;
			}
		}
		nVal = 0;
		return false;
	}

	void set_byName(const unsigned int pid, const valueName valName, const unsigned __int64 nVal)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapNamedULONGLONGs[pid][valName] = nVal;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapNamedULONGLONGs.size() < 1000 && mapNamedULONGLONGs[pid].size() < 1000);
	}

	bool get_byName(const unsigned int pid, const valueName valName, unsigned __int64& nVal)
	{
		std::lock_guard<std::mutex> lock(prot);
		auto& i = mapNamedULONGLONGs.find(pid);
		if (i != mapNamedULONGLONGs.end())
		{
			auto i2 = i->second.find(valName);
			if (i2 != i->second.end())
			{
				nVal = i2->second;
				return true;
			}
		}
		nVal = 0;
		return false;
	}

	// old specific methods, todo: switch to above
	void set_CPUUse(const unsigned int pid, const double cpuUse)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapCPUUse[pid] = cpuUse;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapCPUUse.size() < 1000);
	}
	bool get_CPUUse(const unsigned int pid, double& cpuUse)
	{
		std::lock_guard<std::mutex> lock(prot);
		auto i = mapCPUUse.find(pid);
		if (i == mapCPUUse.end())
		{
			cpuUse = 0;
			return false;
		}
		cpuUse = i->second;
		return true;
	}

	void set_AverageCPU(const unsigned int pid, const double cpu)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapAverageCPU[pid] = cpu;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapAverageCPU.size() < 1000);
	}
	bool get_AverageCPU(const unsigned int pid, double& cpu)
	{
		std::lock_guard<std::mutex> lock(prot);
		auto i = mapAverageCPU.find(pid);
		if (i == mapAverageCPU.end())
		{
			cpu = 0;
			return false;
		}
		cpu = i->second;
		return true;
	}

	void set_PrivateBytes(const unsigned int pid, const unsigned __int64 nPrivateBytes)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapPrivateWorkingSet[pid] = nPrivateBytes;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapPrivateWorkingSet.size() < 1000);
	}
	bool get_PrivateBytes(const unsigned int pid, unsigned __int64& nPrivateBytes)
	{
		std::lock_guard<std::mutex> lock(prot);
		auto i = mapPrivateWorkingSet.find(pid);
		if (i == mapPrivateWorkingSet.end())
		{
			nPrivateBytes = 0;
			return false;
		}
		nPrivateBytes = i->second;
		return true;
	}

};
