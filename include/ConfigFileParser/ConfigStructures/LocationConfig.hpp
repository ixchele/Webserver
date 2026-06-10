#pragma once
#include <CommonConfig.hpp>

struct	LocationConfig : public CommonConfig {
	std::string	path;
	int			methods;
	std::string	upload;

	LocationConfig(void);
	void	resetConf(void);
};
