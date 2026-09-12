// #include <ConfigFile.hpp>
// #include <ConfigFileParser.template.hpp>
// #include <vector>
#include "Logger.hpp"
#include "ServerConfig.hpp"
#include <ConfigParser.hpp>
#include <Logger.hpp>
#include <exception>
#include <iostream>
#include <ostream>
#include <vector>
#include <Server.hpp>
#include <Multiplexer.hpp>
#include <netinet/in.h>

int main(int ac, char **av)
{


	try
	{
		std::string config = "Configs/server.conf";
		if (ac == 2)
			config = av[1];

		Logger::getInstance().setLogFile("/tmp/Webserv.log");

		TokenList tokenList = tokenizer(config);
		ConfigParser lexer(tokenList);
		std::vector<ServerConfig> v_configs;
		v_configs = lexer.parse();

		Multiplexer multiplexer(v_configs);
		ServersMap::iterator it;
		for (it = multiplexer.m_servers.begin(); it != multiplexer.m_servers.end(); ++it)
		{
			LOG_INFO << "server " << it->second->m_key << " is running";
		}
		multiplexer.startup();
	}
	catch (ConfigParser::ConfigException &e)
	{
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}
}
