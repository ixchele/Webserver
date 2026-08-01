#include "HttpStatus.hpp"
#include <RequestHandler.hpp>
#include <cstddef>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <vector>

RequestHandler::RequestHandler(const HttpRequest &request, HttpResponse &response, const ServerConfig &config)
	: _request(request), _response(response), _config(config) {}

	RequestHandler::~RequestHandler() {}

	void	RequestHandler::handle() {

		if (_request.getState() == HttpRequest::ERROR) {
			_buildErrorResponse(_request.getErrorCode());
			return;
		}

		std::string	real_path = _resolvePath(_request.getUri().getPath());

		if (!_isBodySizeValid()) {
			_buildErrorResponse(HttpStatus::PayloadTooLarge); // 413
			return;
		}

		if (!_isMethodAllowed()) {
			_buildErrorResponse(HttpStatus::MethodNotAllowed); // 405
			return;
		}

		switch (_request.getMethod()) {
			case HTTP_GET:
				_handleGet(real_path);
				break;
			case HTTP_POST:
				_handlePost(real_path);
				break;
			case HTTP_DELETE:
				_handleDelete(real_path);
				break;
			default:
				_buildErrorResponse(HttpStatus::NotImplemented); // 501
				break;
		}
	}

void    RequestHandler::_handleGet(const std::string &real_path) {
	struct stat    file_stat;

	// NOTE : check if file exist
	if (stat(real_path.c_str(), &file_stat) != 0) {
		_buildErrorResponse(HttpStatus::NotFound); // NOTE : 404
		return;
	}

	// NOTE : check permessions
	if (access(real_path.c_str(), R_OK) != 0) {
		_buildErrorResponse(HttpStatus::Forbidden); // NOTE : 403
		return;
	}

	// NOTE : is dir ?
	if (S_ISDIR(file_stat.st_mode)) {
		_handleDirectory(real_path, _request.getUri().getPath());
		return;
	}

	// NOTE : is cgi ?
	if (_isCgiExtension(real_path)) {
		_handleCGI(real_path);
		return;
	}

	_response.setStatusCode(HttpStatus::OK); // NOTE : 200

	// _response.setHeader("Content-Type", _guessMimeType(real_path));

	if (!_response.setFileBody(real_path)) {
		_buildErrorResponse(HttpStatus::InternalServerError); // NOTE : 500
		return;
	}

	_response.build();
}


static std::string	findValidIndex(const std::vector<std::string> &indexes, const std::string &real_path) {
	for (std::size_t i = 0; i < indexes.size(); ++i) {
		std::string	test_path = real_path;
		if (test_path[test_path.length() - 1] != '/')
			test_path += "/";
		test_path += indexes[i];

		struct stat	file_stat;
		if (stat(test_path.c_str(), &file_stat) == 0 && access(test_path.c_str(), R_OK) == 0
				&& !S_ISDIR(file_stat.st_mode)) {
			return test_path;
		}
	}
	return "";
}

void	RequestHandler::_handleDirectory(const std::string &real_path, const std::string &uri) {
	const std::vector<std::string>	&indexes = _config.index;
	const std::string				target_index_path = findValidIndex(indexes, real_path);

	if (!target_index_path.empty()) {
		_response.setStatusCode(HttpStatus::OK);
		if (_response.setFileBody(target_index_path))
			_buildErrorResponse(HttpStatus::InternalServerError);
		else
			_response.build();
		return;
	}

	bool	autoindex = _config.autoindex;
}

// void    RequestHandler::_handleDirectory(const std::string &real_path, const std::string &uri) {
//     // 1. Recherche du fichier d'index (ex: index.html)
//     // NOTE: Dans la vraie version, tu demanderas à _config.getIndex(uri)
//     std::string    index_path = real_path;
//     if (index_path[index_path.length() - 1] != '/')
//         index_path += "/";
//     index_path += "index.html"; 
//
//     struct stat    file_stat;
//     // Si index.html existe et qu'on peut le lire
//     if (stat(index_path.c_str(), &file_stat) == 0 && access(index_path.c_str(), R_OK) == 0) {
//         _response.setStatusCode(HttpStatus::OK);
//         // _response.setHeader("Content-Type", "text/html");
//
//         if (!_response.setFileBody(index_path))
//             _buildErrorResponse(HttpStatus::InternalServerError); // 500
//         else
//             _response.build();
//         return;
//     }
//
//     // 2. Si pas d'index, on regarde l'Autoindex
//     // NOTE: Dans la vraie version, tu feras bool autoindex = _config.getAutoIndex(uri);
//     bool    autoindex = true; // Simulé à ON pour l'exemple
//
//     if (!autoindex) {
//         _buildErrorResponse(HttpStatus::Forbidden); // 403
//         return;
//     }
//
//     // 3. Génération de l'Autoindex
//     DIR    *dir = opendir(real_path.c_str());
//     if (dir == NULL) {
//         _buildErrorResponse(HttpStatus::InternalServerError); // 500
//         return;
//     }
//
//     // On commence à construire notre page HTML
//     std::string    html = "<html><head><title>Index of " + uri + "</title></head><body>\n";
//     html += "<h1>Index of " + uri + "</h1><hr><pre>\n";
//
//     struct dirent  *entry;
//     while ((entry = readdir(dir)) != NULL) {
//         std::string    name = entry->d_name;
//
//         // On ignore le dossier courant (mais on garde ".." pour pouvoir remonter)
//         if (name == ".")
//             continue;
//
//         // On construit le lien cliquable (href)
//         std::string    href = uri;
//         if (!href.empty() && href[href.length() - 1] != '/')
//             href += "/";
//         href += name;
//
//         // Si l'entrée est un dossier, on rajoute un petit '/' visuel à la fin du nom
//         if (entry->d_type == DT_DIR)
//             name += "/";
//
//         html += "<a href=\"" + href + "\">" + name + "</a>\n";
//     }
//     closedir(dir);
//
//     html += "</pre><hr></body></html>";
//
//     // 4. On insère notre HTML généré dans la réponse
//     _response.setStatusCode(HttpStatus::OK);
//     _response.setHeader("Content-Type", "text/html");
//     _response.setBody(html); // setBody mettra automatiquement à jour le Content-Length !
//     _response.build();
// }
