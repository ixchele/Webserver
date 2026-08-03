#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

struct	CommonConfig {
	std::string							root;
	std::vector<std::string>			index;
	bool								autoindex;
	int									client_max_body_size;
	int									methods;
	std::map<std::string, std::string>	cgi_pass;
	std::map<int, std::string>			error_page;
	std::string							return_val;

	CommonConfig(void);
	virtual	~CommonConfig(void);

	void	resetConf(void);
	virtual std::string	str(const std::string& indent = "  ") const;
};
