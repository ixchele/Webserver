#include <ConfigFile.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <deque>
#include <string>
#include <utility>
#include <vector>

typedef std::deque<Token> TokenList;

void	consume(TokenList &tokenList, std::string expected) {
	if (tokenList.empty() || tokenList[0].content != expected)
		throw std::runtime_error("Expected '" + expected + "'");

	tokenList.pop_front();
}

// NOTE : map of server_directives
typedef std::map<std::string, void(*)(TokenList &)>  ServerDirectives;
typedef std::map<std::string, void(*)(TokenList &)>  LocationDirectives;

ServerDirectives	serverDir;
LocationDirectives	locationDir;

// NOTE : methods defines
// TODO : put them some where else
#define GET 1
#define POST 2
#define DELETE 4

typedef std::vector<std::pair<std::string, int> > MethodsVec;
void	methodsDir(TokenList &tokenList) {
	consume(tokenList, "methods");

	std::map<std::string, int>	methodsAvailable;
	methodsAvailable["GET"] = GET;
	methodsAvailable["POST"] = POST;
	methodsAvailable["DELETE"] = DELETE;


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
	consume(tokenList, ";");
}

void	rootDir(TokenList &tokenList) {
	consume(tokenList, "root");

	std::string	root;
	std::stringstream	ssRoot(tokenList[0].content);

	if (!(ssRoot >> root) || !ssRoot.eof())
		throw std::runtime_error("invalid root value");

	//  WARN : should parse the input first
	std::cout << "root : "<< root << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	indexDir(TokenList &tokenList) {
	consume(tokenList, "index");

	std::string	index;
	std::stringstream	ssIndex(tokenList[0].content);

	if (!(ssIndex >> index) || !ssIndex.eof())
		throw std::runtime_error("invalid root value");

	//  WARN : should parse the input first
	std::cout << "index : " << index << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	autoindexDir(TokenList &tokenList) {
	consume(tokenList, "autoindex");

	std::string	autoindex;
	std::stringstream	ssAutoindex(tokenList[0].content);

	if (!(ssAutoindex >> autoindex) || !ssAutoindex.eof())
		throw std::runtime_error("invalid autoindex value");

	//  WARN : should parse the input first
	std::cout << "autoindex : " << autoindex << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	returnDir(TokenList &tokenList) {
	consume(tokenList, "return");

	int					statusCode;
	std::stringstream	ssStatusCode(tokenList[0].content);

	if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof())
		throw std::runtime_error("invalid status code value"); 

	tokenList.pop_front();

	std::string			pathPage;
	std::stringstream	ssPathPage(tokenList[0].content);

	if (!(ssPathPage >> pathPage) || !ssPathPage.eof())
		throw std::runtime_error("invalid status code value"); 

	//  WARN : should parse the input first
	std::cout << "return : " << statusCode << " " << pathPage << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	uploadDir(TokenList &tokenList) {
	consume(tokenList, "upload_path");

	std::string	uploadPath;
	std::stringstream	ssUploadPath(tokenList[0].content);

	if (!(ssUploadPath >> uploadPath) || !ssUploadPath.eof())
		throw std::runtime_error("invalid host value"); 

	//  WARN : should parse the input first
	std::cout << "upload_path : "<< uploadPath << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	cgiPassDir(TokenList &tokenList) {
	(void)tokenList;
}


void	listenDir(TokenList &tokenList) {
	consume(tokenList, "listen");

	int	port;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> port) || !ssPort.eof())
		throw std::runtime_error("invalid port value");

	//  WARN : should parse the input first
	std::cout << "listen : "<< port << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	hostDir(TokenList &tokenList) {
	consume(tokenList, "host");

	std::string	host;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> host) || !ssPort.eof())
		throw std::runtime_error("invalid host value"); 

	//  WARN : should parse the input first
	std::cout << "host : "<< host << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	nameDir(TokenList &tokenList) {
	consume(tokenList, "server_name");

	std::string			servName;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> servName) || !ssPort.eof())
		throw std::runtime_error("invalid host value"); 

	//  WARN : should parse the input first
	std::cout << "server_name : "<< servName << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	errorPageDir(TokenList &tokenList) {
	consume(tokenList, "error_page");

	int					statusCode;
	std::stringstream	ssStatusCode(tokenList[0].content);

	if (!(ssStatusCode >> statusCode) || !ssStatusCode.eof())
		throw std::runtime_error("invalid status code value"); 

	tokenList.pop_front();

	std::string			pathPage;
	std::stringstream	ssPathPage(tokenList[0].content);

	if (!(ssPathPage >> pathPage) || !ssPathPage.eof())
		throw std::runtime_error("invalid status code value"); 

	//  WARN : should parse the input first
	std::cout << "error_page : " << statusCode << " " << pathPage << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	clientBodyDir(TokenList &tokenList) {
	consume(tokenList, "client_max_body_size");

	std::string			size;
	std::stringstream	ssPort(tokenList[0].content);

	if (!(ssPort >> size) || !ssPort.eof())
		throw std::runtime_error("invalid size value"); 

	 // WARN : should parse the input first
	std::cout << "body size : "<< size << std::endl;

	tokenList.pop_front();
	consume(tokenList, ";");
}

void	locationDirective(TokenList &tokenList) {
	std::string	keyword = tokenList[0].content;

	if(locationDir.count(keyword))
		locationDir[keyword](tokenList);
}

void	locationBlock(TokenList &tokenList) {
	consume(tokenList, "location");

	std::string			path;
	std::stringstream	ssPath(tokenList[0].content);

	if (!(ssPath >> path) || !ssPath.eof())
		throw std::runtime_error("invalid size value"); 

	 // WARN : should parse the input first
	std::cout << "location path : " << path << std::endl;

	tokenList.pop_front();

	consume(tokenList, "{");

	while (!tokenList.empty() && tokenList[0].content != "}")
	{
		locationDirective(tokenList);
	}

	consume(tokenList, "}");
}

void	serverDirective(TokenList &tokenList) {
	std::string	keyword = tokenList[0].content;

	if(serverDir.count(keyword))
		serverDir[keyword](tokenList);
}

void	serverBlock(TokenList &tokenList) {
	if (tokenList[0].content != "server")
		return;

	consume(tokenList, "server");
	consume(tokenList, "{");

	while (!tokenList.empty() && tokenList[0].content != "}")
		serverDirective(tokenList);

	consume(tokenList, "}");
}

void	config(TokenList &tokenList) {
	serverDir["listen"] = listenDir;
	serverDir["host"] = hostDir;
	serverDir["server_name"] = nameDir;
	serverDir["error_page"] = errorPageDir;
	serverDir["client_max_body_size"] = clientBodyDir;
	serverDir["location"] = locationBlock;

	locationDir["methods"] = methodsDir;
	locationDir["root"] = rootDir;
	locationDir["index"] = indexDir;
	locationDir["autoindex"] = autoindexDir;
	locationDir["return"] = returnDir;
	locationDir["upload_path"] = uploadDir;
	locationDir["cgi_extension"] = cgiPassDir;

	while (!tokenList.empty()) {
		serverBlock(tokenList);
		if (!tokenList.empty() && tokenList[0].content != "server")
			throw std::runtime_error("invalid keyword '" + tokenList[0].content + "'"); // TODO : create excpetion for config file errors
	}
}

static std::string	extractFileContent(const std::string &confPath) {
	std::ifstream	confIfs(confPath.c_str());
	if (!confIfs.is_open())
		throw std::runtime_error("[x] Error : could not open file \'" + confPath + "'" + ".");
	
	std::stringstream	buff;
	buff << confIfs.rdbuf();

	return buff.str();
}

void	configParser(const std::string &confPath) {
	std::string	confContent = extractFileContent(confPath);
	TokenList	tokenList = tokenizer(confContent);

	config(tokenList);
}
