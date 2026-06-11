#pragma once
#include <CommonConfig.hpp>

struct	LocationConfig : public CommonConfig {
	std::string	path;
	int			methods;
	std::string	upload;

	LocationConfig(void);
	void	resetConf(void);

	virtual std::string toString(const std::string& indent = "  ") const;
};
