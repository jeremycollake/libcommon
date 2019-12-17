#pragma once

// TODO: template this if continued use (each data-type will have an instance)

#include <unordered_map>
#include <mutex>

class ProcessCache
{	
public:
	enum valueName {
		CacheValThreadCount,
		CacheValIODelta
	};
private:
	std::mutex prot;
	std::unordered_map<unsigned int, double> mapCPUUse;
	std::unordered_map<unsigned int, unsigned __int64> mapPrivateWorkingSet;
	std::unordered_map<unsigned int, unordered_map<valueName, unsigned long>> mapNamedULONGs;
	std::unordered_map<unsigned int, unordered_map<valueName, unsigned __int64>> mapNamedULONGLONGs;
public:
	void erase(const unsigned int pid)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapCPUUse.erase(pid);
		mapPrivateWorkingSet.erase(pid);
		mapNamedULONGs.erase(pid);
		mapNamedULONGLONGs.erase(pid);
	}

	void add_CPUUse(const unsigned int pid, const double cpuUse)
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

	void add_PrivateBytes(const unsigned int pid, const unsigned __int64 nPrivateBytes)
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
	
	void add_byname(const unsigned int pid, const valueName valName, const unsigned long nVal)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapNamedULONGs[pid][valName] = nVal;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapNamedULONGs.size() < 1000 && mapNamedULONGs[pid].size() < 1000);
	}
	bool get_byname(const unsigned int pid, const valueName valName, unsigned long &nVal)
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

	void add_byname(const unsigned int pid, const valueName valName, const unsigned __int64 nVal)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapNamedULONGLONGs[pid][valName] = nVal;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapNamedULONGLONGs.size() < 1000 && mapNamedULONGLONGs[pid].size() < 1000);
	}
	
	bool get_byname(const unsigned int pid, const valueName valName, unsigned __int64& nVal)
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


};
