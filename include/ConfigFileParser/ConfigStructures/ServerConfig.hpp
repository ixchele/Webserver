#pragma once
#include <CommonConfig.hpp>
#include <LocationConfig.hpp>
#include <vector>

struct	ServerConfig : public CommonConfig {
	std::vector<int>			listen;
	std::vector<std::string>	hosts;
	std::vector<std::string>	names;
	std::vector<LocationConfig>	locations;

	ServerConfig(void);
	~ServerConfig(void);

	void	resetConf(void);
	virtual std::string str(const std::string& indent = "") const;
	void	applyInheritance();
};
