#pragma once
#include <CommonConfig.hpp>
#include <LocationConfig.hpp>
#include <vector>

struct	ServerConfig : public CommonConfig {
	std::vector<int>			listen;
	std::string					host;
	std::string					name;
	std::vector<LocationConfig>	locations;

	ServerConfig(void);
	~ServerConfig(void);

	void	resetConf(void);
};
