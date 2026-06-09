#include <ServerConfig.hpp>
#include <ConfigParser.hpp>
#include <Token.hpp>
#include <iostream>
#include <ConfigFileParser.template.hpp>
#include <vector>

std::ostream& operator<<(std::ostream& os, const Token& t) {
	os << "Token[" << t.line << ":" << t.column << "] -> \"" << t.content << "\"" << std::endl;
	return os;
}

ConfigParser::ConfigParser(const TokenList &tokenList)
	: tokenList(tokenList), it(tokenList.begin()){

	PrintContainer(tokenList); //  BUG : for debug

	this->serverDirectives["listen"] = &ConfigParser::listenDir;
	this->serverDirectives["host"] = &ConfigParser::hostDir;
	this->serverDirectives["server_name"] = &ConfigParser::nameDir;
	this->serverDirectives["error_page"] = &ConfigParser::errorPageDir;
	this->serverDirectives["location"] = &ConfigParser::locationBlock;

	this->locationDirectives["methods"] = &ConfigParser::methodsDir;
	this->locationDirectives["return"] = &ConfigParser::returnDir;
	this->locationDirectives["upload_path"] = &ConfigParser::uploadDir;

	this->communDirectives["root"] = &ConfigParser::rootDir;
	this->communDirectives["index"] = &ConfigParser::indexDir;
	this->communDirectives["autoindex"] = &ConfigParser::autoindexDir;
	this->communDirectives["client_max_body_size"] = &ConfigParser::clientBodyDir;
	this->communDirectives["cgi_extension"] = &ConfigParser::cgiPassDir;
	// this->communDirectives["cgi_path"] = ;  TODO : implement cgi_path directive 

	this->methodsAvailable["GET"] = GET;
	this->methodsAvailable["POST"] = POST;
	this->methodsAvailable["DELETE"] = DELETE;
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

	int	method = 0;
	while (this->currentContent() != ";") {
		if (!methodsAvailable.count(this->currentContent()))
			throw ConfigException("invalid method", *this->it);

		method |= methodsAvailable[this->currentContent()];

		consume();
	}

	std::cout << "methods : " <<  method << std::endl;
	consume(";");
}

void	ConfigParser::rootDir() {
	consume("root");

	std::string	root;
	std::stringstream	ssRoot(this->currentContent());

	if (!(ssRoot >> root) || !ssRoot.eof())
		throw ConfigException("invalid root", *this->it);

	std::cout << "root : "<< root << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::indexDir() {
	consume("index");

	std::string	index;
	std::stringstream	ssIndex(this->currentContent());

	if (!(ssIndex >> index) || !ssIndex.eof())
		throw ConfigException("invalid index", *this->it);

	std::cout << "index : " << index << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::autoindexDir() {
	consume("autoindex");

	std::string	autoindex;
	std::stringstream	ssAutoindex(this->currentContent());

	if (!(ssAutoindex >> autoindex) || !ssAutoindex.eof())
		throw ConfigException("invalid autoindex", *this->it);

	std::cout << "autoindex : " << autoindex << std::endl;

	consume();
	consume(";");
}

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

	std::cout << "return : " << statusCode << " " << pathPage << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::uploadDir() {
	consume("upload_path");

	std::string	uploadPath;
	std::stringstream	ssUploadPath(this->currentContent());

	if (!(ssUploadPath >> uploadPath) || !ssUploadPath.eof())
		throw ConfigException("invalid upload path", *this->it);

	std::cout << "upload_path : "<< uploadPath << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::cgiPassDir() {

}


void	ConfigParser::listenDir() {
	consume("listen");


	int	port;
	std::stringstream	ssPort(this->currentContent());

	if (!(ssPort >> port) || !ssPort.eof())
		throw ConfigException("invalid port value", *this->it);


	this->tmpServer.listen.push_back(port);
	std::cout << "listen : " << port << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::hostDir() {
	consume("host");

	std::string	host;
	std::stringstream	ssPort(this->currentContent());

	if (!(ssPort >> host) || !ssPort.eof())
		throw ConfigException("invalid host", *this->it); 


	this->tmpServer.host = host;
	std::cout << "host : "<< host << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::nameDir() {
	consume("server_name");

	std::string			servName;
	std::stringstream	ssPort(this->currentContent());

	if (!(ssPort >> servName) || !ssPort.eof())
		throw ConfigException("invalid server name", *this->it); 

	this->tmpServer.name = servName;
	std::cout << "server_name : "<< servName << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::errorPageDir() {
	consume("error_page");

	int					statusCode;
	std::stringstream	ssStatusCode(this->currentContent());

	if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof())
		throw ConfigException("invalid error code", *this->it); 

	consume();

	std::string			pathPage;
	std::stringstream	ssPathPage(this->currentContent());

	if (!(ssPathPage >> pathPage) || !ssPathPage.eof())
		throw ConfigException("invalid error page path", *this->it); 

	std::cout << "error_page : " << statusCode << " " << pathPage << std::endl;

	consume();
	consume(";");
}

void	ConfigParser::clientBodyDir() {
	consume("client_max_body_size");

	std::string			size;
	std::stringstream	ssPort(this->currentContent());

	if (!(ssPort >> size) || !ssPort.eof())
		throw ConfigException("invalid client body size", *this->it); 

	std::cout << "body size : "<< size << std::endl;

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

	std::string			path;
	std::stringstream	ssPath(this->currentContent());

	if (!(ssPath >> path) || !ssPath.eof())
		throw ConfigException("invalid location path", *this->it); 

	std::cout << "location path : " << path << std::endl;

	consume();
	consume("{");

	while (this->currentContent() != "}")
		locationDirective();

	consume("}");
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

	while (this->currentContent() != "}")
		serverDirective();

	consume("}");
}

void	ConfigParser::config() {
	while (this->it != this->tokenList.end()) {
		serverBlock();
		this->servers.push_back(this->tmpServer);
		this->tmpServer.resetConf();
	}
}

const std::vector<ServerConfig>	ConfigParser::parse(void) {
	this->config();
	return this->servers;
}
