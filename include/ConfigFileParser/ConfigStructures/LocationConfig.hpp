#pragma once
#include <CommonConfig.hpp>
#include <HttpMethod.hpp>

struct	LocationConfig : public CommonConfig {
	std::string	path;
	std::string	upload;

	LocationConfig(void);
	void	resetConf(void);

	virtual std::string toString(const std::string& indent = "  ") const;
};
