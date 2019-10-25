#pragma once

#include <unordered_map>
#include <mutex>

class ProcessCache
{	
	std::mutex prot;
	std::unordered_map<unsigned int, double> mapCPUUse;
public:
	void add_CPUUse(const unsigned int pid, const double cpuUse)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapCPUUse[pid] = cpuUse;
		// a safety catch for infinite growth (client didn't call erase)
		_ASSERT(mapCPUUse.size() < 1000);
	}
	void erase(const unsigned int pid)
	{
		std::lock_guard<std::mutex> lock(prot);
		mapCPUUse.erase(pid);
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
};
