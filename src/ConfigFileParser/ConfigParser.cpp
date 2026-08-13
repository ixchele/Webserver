#include "CommonConfig.hpp"
#include <ServerConfig.hpp>
#include <ConfigParser.hpp>
#include <Token.hpp>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <ConfigFileParser.template.hpp>
#include <vector>

std::ostream& operator<<(std::ostream& os, const Token& t) {
	os << "Token[" << t.line << ":" << t.column << "] -> \"" << t.content << "\"" << std::endl;
	return os;
}

ConfigParser::ConfigParser(const TokenList &tokenList)
	: tokenList(tokenList), it(tokenList.begin()){

	this->serverDirectives["listen"] = &ConfigParser::listenDir;
	this->serverDirectives["host"] = &ConfigParser::hostDir;
	this->serverDirectives["server_name"] = &ConfigParser::nameDir;
	this->serverDirectives["location"] = &ConfigParser::locationBlock;

	this->locationDirectives["upload_path"] = &ConfigParser::uploadDir;

	this->communDirectives["methods"] = &ConfigParser::methodsDir;
	this->communDirectives["root"] = &ConfigParser::rootDir;
	this->communDirectives["index"] = &ConfigParser::indexDir;
	this->communDirectives["autoindex"] = &ConfigParser::autoindexDir;
	this->communDirectives["client_max_body_size"] = &ConfigParser::clientBodyDir;
	this->communDirectives["cgi_pass"] = &ConfigParser::cgiPass;
	this->communDirectives["error_page"] = &ConfigParser::errorPageDir;
	this->communDirectives["return"] = &ConfigParser::returnDir;

	this->methodsAvailable["GET"] = HTTP_GET;
	this->methodsAvailable["POST"] = HTTP_POST;
	this->methodsAvailable["DELETE"] = HTTP_DELETE;
}

ConfigParser::~ConfigParser(void) {
	//
}

// NOTE : Helpers

// ----------------------------
bool	ConfigParser::peek(const std::string &expected = "") {
	if (expected == "")
		return this->it == this->tokenList.end(); 
	if (this->it == this->tokenList.end())
		return false;
	return this->it->content == expected;
}

void ConfigParser::consume(const std::string &expected = "") {
	if (this->it == this->tokenList.end())
        throw ConfigException("Unexpected end of file: expected '" + expected + "'");
	else if (expected != "" && this->it->content != expected)
		throw ConfigException("Expected '" + expected + "'", *it);

    this->it++;
}

std::string	ConfigParser::currentContent(void) const {
	if (this->it == this->tokenList.end())
		throw ConfigException("accessing token at end of list");

	return this->it->content;
}
// ----------------------------

void	ConfigParser::methodsDir() {
	consume("methods");

	int	methods = 0;
	while (this->currentContent() != ";") {
		if (!methodsAvailable.count(this->currentContent()))
			throw ConfigException("invalid method", *this->it);

		methods |= methodsAvailable[this->currentContent()];

		consume();
	}

	this->currentBlock->methods = methods;

	consume(";");
}

void	ConfigParser::rootDir() {
	consume("root");

	std::string	root;
	std::stringstream	ssRoot(this->currentContent());

	if (!(ssRoot >> root) || !ssRoot.eof())
		throw ConfigException("invalid root", *this->it);

	this->currentBlock->root = root;

	consume();
	consume(";");
}

void    ConfigParser::indexDir() {
	consume("index");

	while (this->currentContent() != ";") {
		std::string			index;
		std::stringstream	ssIndex(this->currentContent());

		if (!(ssIndex >> index) || !ssIndex.eof())
			throw ConfigException("invalid index", *this->it);

		this->currentBlock->index.push_back(index);

		consume();
	}

	consume(";");
}

void	ConfigParser::autoindexDir() {
	consume("autoindex");

	std::string	autoindex;
	std::stringstream	ssAutoindex(this->currentContent());

	if (!(ssAutoindex >> autoindex) || !ssAutoindex.eof())
		throw ConfigException("invalid autoindex", *this->it);

	if (autoindex != "off" && autoindex != "on")
		throw ConfigException("autoindex should be on|off", *this->it);

	this->currentBlock->autoindex = autoindex == "on" ? true : false;

	consume();
	consume(";");
}

// BUG : should save statusCode !!
void	ConfigParser::returnDir() {
	consume("return");

	int					statusCode;
	std::stringstream	ssStatusCode(this->currentContent());

	if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof())
		throw ConfigException("invalid return statusCode", *this->it);

	consume();

	std::string			pathPage;
	std::stringstream	ssPathPage(this->currentContent());

	if (!(ssPathPage >> pathPage) || !ssPathPage.eof())
		throw ConfigException("invalid return path", *this->it);

	this->currentBlock->return_val = pathPage;

	consume();
	consume(";");
}

void	ConfigParser::uploadDir() {
	consume("upload_path");

	std::string	uploadPath;
	std::stringstream	ssUploadPath(this->currentContent());

	if (!(ssUploadPath >> uploadPath) || !ssUploadPath.eof())
		throw ConfigException("invalid upload path", *this->it);

	this->tmpLocation.upload = uploadPath;

	consume();
	consume(";");
}

void	ConfigParser::cgiPass() {
	consume("cgi_pass");

	std::string	cgiExtension;
	std::stringstream	ssCgiExtension(this->currentContent());

	if (!(ssCgiExtension >> cgiExtension) || !ssCgiExtension.eof())
		throw ConfigException("invalid cgi extension", *this->it);

	consume();

	std::string	cgiPath;
	std::stringstream	ssCgiPath(this->currentContent());

	if (!(ssCgiPath >> cgiPath) || !ssCgiPath.eof())
		throw ConfigException("invalid cgi path", *this->it);

	this->currentBlock->cgi_pass[cgiExtension] = cgiPath;

	consume();
	consume(";");
}

void	ConfigParser::listenDir() {
	consume("listen");

	int	port;
	std::stringstream	ssPort(this->currentContent());

	if (!(ssPort >> port) || !ssPort.eof() || !(port >= 0 && port <= 65535))
		throw ConfigException("invalid port value", *this->it);

	this->tmpServer.listen.push_back(port);

	consume();
	consume(";");
}

void	ConfigParser::hostDir() {
	consume("host");

	std::string	host;
	std::stringstream	ssPort(this->currentContent());

	if (!(ssPort >> host) || !ssPort.eof())
		throw ConfigException("invalid host", *this->it); 

	this->tmpServer.hosts.push_back(host);

	consume();
	consume(";");
}

void	ConfigParser::nameDir() {
	consume("server_name");

	std::vector<std::string>	servNames;

	while (this->currentContent() != ";") {
		std::string			servName;
		std::stringstream	ssPort(this->currentContent());

		if (!(ssPort >> servName) || !ssPort.eof())
			throw ConfigException("invalid server name", *this->it); 

		servNames.push_back(servName);
		consume();
	}

	this->tmpServer.names = servNames;

	consume(";");
}

void	ConfigParser::errorPageDir() {
	consume("error_page");

	std::vector<std::string> args;

	while (this->currentContent() != ";") {
		args.push_back(this->currentContent());
		consume();
	}

	if (args.size() < 2)
		throw ConfigException("error_page requires at least 2 arguments", *this->it);

	consume(";");

	std::string pathPage = args.back();

	for (size_t i = 0; i < args.size() - 1; ++i) {
		int statusCode;
		std::stringstream ssStatusCode(args[i]);

		if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof()) {
			throw ConfigException("invalid error code", *this->it); 
		}

		this->currentBlock->error_page[statusCode] = pathPage;
	}
}

void	ConfigParser::clientBodyDir() {
	consume("client_max_body_size");

	std::size_t			size;
	char				unit = 'k';
	std::stringstream	ssBodySize(this->currentContent());

	if (!(ssBodySize >> size))
		throw ConfigException("invalid client body size", *this->it); 

	ssBodySize >> unit;
	if (std::string("KMGkmg").find(unit) == std::string::npos)
		throw ConfigException("invalid size unit", *this->it);

	unit = std::tolower(unit);
	switch (unit) {
		case 'k': this->currentBlock->client_max_body_size = size * 1024; break;
		case 'm': this->currentBlock->client_max_body_size = size * std::pow(1024, 2); break;
		case 'g': this->currentBlock->client_max_body_size = size * std::pow(1024, 3); break;
	}

	consume();
	consume(";");
}

void	ConfigParser::locationDirective() {
	std::string	keyword = this->currentContent();

	if(locationDirectives.count(keyword))
		(this->*locationDirectives[keyword])();
	else if(communDirectives.count(keyword))
		(this->*communDirectives[keyword])();
	else
		throw ConfigException("unkonwn loaction directive", *this->it); 
}

void	ConfigParser::locationBlock() {
	consume("location");

    CommonConfig	*previousBlock = this->currentBlock; 
    this->tmpLocation.resetConf();
    this->currentBlock = &this->tmpLocation;

	std::string			path;
	std::stringstream	ssPath(this->currentContent());

	if (!(ssPath >> path) || !ssPath.eof())
		throw ConfigException("invalid location path", *this->it); 

	this->tmpLocation.path = path;

	consume();
	consume("{");

	while (this->currentContent() != "}")
		locationDirective();

	consume("}");

    this->tmpServer.locations.push_back(this->tmpLocation);
    this->currentBlock = previousBlock; 
}

void	ConfigParser::serverDirective() {
	std::string	keyword = this->currentContent();

	if(this->serverDirectives.count(keyword))
		(this->*serverDirectives[keyword])();
	else if(this->communDirectives.count(keyword))
		(this->*communDirectives[keyword])();
	else
		throw ConfigException("unkonwn server directive", *this->it); 
}

void	ConfigParser::serverBlock() {

	consume("server");
	consume("{");

	this->tmpServer.resetConf();
	this->currentBlock = &this->tmpServer;

	while (this->currentContent() != "}")
		serverDirective();

	consume("}");
}

void	ConfigParser::config() {
	while (this->it != this->tokenList.end() && this->it->content != "") {
		serverBlock();
		tmpServer.applyInheritance();
		this->servers.push_back(this->tmpServer);
	}
}

const std::vector<ServerConfig>	ConfigParser::parse(void) {
	this->config();
	return this->servers;
}

std::ostream& operator<<(std::ostream& os, const CommonConfig& conf) {
    os << conf.str();
    return os;
}
