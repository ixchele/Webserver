#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

struct	CommonConfig {
	std::string					root;
	std::vector<std::string>	index;
	bool						autoindex;
	std::size_t					client_max_body_size;
	std::string					cgi_extension;
	std::string					cgi_path;
	std::map<int, std::string>	error_page;
	std::string					return_val;

	CommonConfig(void);
	virtual	~CommonConfig(void);

	void	resetConf(void);
};
