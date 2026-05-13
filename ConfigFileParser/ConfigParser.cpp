#include "ConfigParser.hpp"
#include "Token.hpp"
#include <stdexcept>

ConfigParser::ConfigParser(const TokenList &tokenList)
	: tokenList(tokenList), it(tokenList.begin()){

	this->serverDirectives["listen"] = &ConfigParser::listenDir;
	this->serverDirectives["host"] = &ConfigParser::hostDir;
	this->serverDirectives["server_name"] = &ConfigParser::nameDir;
	this->serverDirectives["error_page"] = &ConfigParser::errorPageDir;
	this->serverDirectives["client_max_body_size"] = &ConfigParser::clientBodyDir;
	this->serverDirectives["location"] = &ConfigParser::locationBlock;

	this->locationDirectives["methods"] = &ConfigParser::methodsDir;
	this->locationDirectives["root"] = &ConfigParser::rootDir;
	this->locationDirectives["index"] = &ConfigParser::indexDir;
	this->locationDirectives["autoindex"] = &ConfigParser::autoindexDir;
	this->locationDirectives["return"] = &ConfigParser::returnDir;
	this->locationDirectives["upload_path"] = &ConfigParser::uploadDir;
	this->locationDirectives["cgi_extension"] = &ConfigParser::cgiPassDir;

	this->methodsAvailable["GET"] = GET;
	this->methodsAvailable["POST"] = POST;
	this->methodsAvailable["DELETE"] = DELETE;
}

bool	ConfigParser::peek(const std::string &expected) {
	if (this->it == this->tokenList.end())
		return false;
	return this->it->content == expected;
}

void ConfigParser::consume(const std::string &expected = "") {
	if (expected == "")
		;
	else if (this->it == this->tokenList.end())
        throw std::runtime_error("Unexpected end of file: expected '" + expected + "'");

	else if (this->it->content != expected) {
        std::stringstream errorMessage;
        errorMessage << "Configuration error at line " << it->line
           << ", col " << it->column
           << ": expected '" << expected
           << "' but found '" << it->content << "'";
        throw std::runtime_error(errorMessage.str());
    }

    this->it++;
}


void	ConfigParser::methodsDir() {
	consume("methods");

	std::string	token = tokenList[0].content;

	int	method = 0;
	while (!token.empty() && token != ";") {
		if (!methodsAvailable.count(token))
			throw std::runtime_error("invalid method");

		method |= methodsAvailable[token];

		tokenList.pop_front();
		token = tokenList[0].content;
	}

	std::cout << "methods : " <<  method << std::endl;
	consume(";");
}

void	ConfigParser::rootDir() {
	consume("root");

	std::string	root;
	std::stringstream	ssRoot(tokenList[0].content);

	if (!(ssRoot >> root) || !ssRoot.eof())
		throw std::runtime_error("invalid root value");

	std::cout << "root : "<< root << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::indexDir() {
	consume("index");

	std::string	index;
	std::stringstream	ssIndex(tokenList[0].content);

	if (!(ssIndex >> index) || !ssIndex.eof())
		throw std::runtime_error("invalid root value");

	std::cout << "index : " << index << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::autoindexDir() {
	consume("autoindex");

	std::string	autoindex;
	std::stringstream	ssAutoindex(tokenList[0].content);

	if (!(ssAutoindex >> autoindex) || !ssAutoindex.eof())
		throw std::runtime_error("invalid autoindex value");

	std::cout << "autoindex : " << autoindex << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::returnDir() {
	consume("return");

	int					statusCode;
	std::stringstream	ssStatusCode(tokenList[0].content);

	if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof())
		throw std::runtime_error("invalid status code value"); 

	tokenList.pop_front();

	std::string			pathPage;
	std::stringstream	ssPathPage(tokenList[0].content);

	if (!(ssPathPage >> pathPage) || !ssPathPage.eof())
		throw std::runtime_error("invalid status code value"); 

	std::cout << "return : " << statusCode << " " << pathPage << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::uploadDir() {
	consume("upload_path");

	std::string	uploadPath;
	std::stringstream	ssUploadPath(tokenList[0].content);

	if (!(ssUploadPath >> uploadPath) || !ssUploadPath.eof())
		throw std::runtime_error("invalid host value"); 

	std::cout << "upload_path : "<< uploadPath << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	cgiPassDir(TokenList &tokenList) {
	(void)tokenList;
}


void	ConfigParser::listenDir() {
	consume("listen");

	std::vector<int>	ports;

	while (!tokenList[0].content.empty() && tokenList[0].content != ";") {
		int	port;
		std::stringstream	ssPort(tokenList[0].content);

		if (!(ssPort >> port) || !ssPort.eof())
			throw std::runtime_error("invalid port value");

		ports.push_back(port);
		tokenList.pop_front();
	}

	std::cout << "listen : ";
	PrintContainer(ports, "ports");

	consume(";");
}

void	ConfigParser::hostDir() {
	consume("host");

	std::string	host;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> host) || !ssPort.eof())
		throw std::runtime_error("invalid host value"); 

	std::cout << "host : "<< host << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::nameDir() {
	consume("server_name");

	std::string			servName;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> servName) || !ssPort.eof())
		throw std::runtime_error("invalid host value"); 

	std::cout << "server_name : "<< servName << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::errorPageDir() {
	consume("error_page");

	int					statusCode;
	std::stringstream	ssStatusCode(tokenList[0].content);

	if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof())
		throw std::runtime_error("invalid status code value"); 

	tokenList.pop_front();

	std::string			pathPage;
	std::stringstream	ssPathPage(tokenList[0].content);

	if (!(ssPathPage >> pathPage) || !ssPathPage.eof())
		throw std::runtime_error("invalid status code value"); 

	std::cout << "error_page : " << statusCode << " " << pathPage << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::clientBodyDir() {
	consume("client_max_body_size");

	std::string			size;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> size) || !ssPort.eof())
		throw std::runtime_error("invalid size value"); 

	std::cout << "body size : "<< size << std::endl;

	tokenList.pop_front();
	consume(";");
}

void	ConfigParser::locationDirective() {
	std::string	keyword = tokenList[0].content;

	if(locationDirectives.count(keyword))
		(this->*locationDirectives[keyword])();
}

void	ConfigParser::locationBlock() {
	consume("location");

	std::string			path;
	std::stringstream	ssPath(tokenList[0].content);

	if (!(ssPath >> path) || !ssPath.eof())
		throw std::runtime_error("invalid size value"); 

	std::cout << "location path : " << path << std::endl;

	tokenList.pop_front();

	consume("{");

	while (!tokenList.empty() && tokenList[0].content != "}")
		locationDirective();

	consume("}");
}

void	ConfigParser::serverDirective() {
	std::string	keyword = tokenList[0].content;

	if(this->communDirectives.count(keyword))
		(this->*serverDirectives[keyword])();
	else
		throw std::runtime_error("unkonw keyword : " + keyword);
}

void	ConfigParser::serverBlock() {
	if (tokenList[0].content != "server")
		return;

	consume("server");
	consume("{");

	while (!tokenList.empty() && tokenList[0].content != "}")
		serverDirective();

	consume("}");
}

void	ConfigParser::config() {
	while (!tokenList.empty()) {
		serverBlock();
		if (!tokenList.empty() && tokenList[0].content != "server")
			throw std::runtime_error("invalid keyword '" + tokenList[0].content + "'"); // TODO : create excpetion for config file errors
	}
}
