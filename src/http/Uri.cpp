#include <Uri.hpp>
#include <sstream>
#include <cstdlib>

Uri::Uri() : _port(0) {}

Uri::Uri(const std::string& raw_uri) : _port(0) {
	parse(raw_uri);
}

Uri::~Uri() {}

void Uri::clear() {
	_original.clear();
	_scheme.clear();
	_host.clear();
	_port = 0;
	_path.clear();
	_query.clear();
	_fragment.clear();
}

bool Uri::parse(const std::string& raw_uri) {
	clear();
	if (raw_uri.empty()) return false;

	_original = raw_uri;
	std::string working_uri = raw_uri;

	size_t hash_pos = working_uri.find('#');
	if (hash_pos != std::string::npos) {
		_fragment = working_uri.substr(hash_pos + 1);
		working_uri.erase(hash_pos);
	}

	size_t scheme_pos = working_uri.find("://");
	if (scheme_pos != std::string::npos) {
		_scheme = working_uri.substr(0, scheme_pos);
		working_uri.erase(0, scheme_pos + 3);

		size_t path_pos = working_uri.find('/');
		std::string authority = (path_pos == std::string::npos) ? working_uri : working_uri.substr(0, path_pos);

		size_t colon_pos = authority.find(':');
		if (colon_pos != std::string::npos) {
			_host = authority.substr(0, colon_pos);

			std::istringstream iss(authority.substr(colon_pos + 1));
			iss >> _port;
		} else {
			_host = authority;
			_port = 80;
		}

		if (path_pos != std::string::npos) {
			working_uri.erase(0, path_pos);
		} else {
			working_uri = "/";
		}
	}

	if (working_uri.empty() || working_uri[0] != '/') {
		return false;
	}

	size_t question_pos = working_uri.find('?');
	if (question_pos != std::string::npos) {
		_path = working_uri.substr(0, question_pos);
		_query = working_uri.substr(question_pos + 1);
	} else {
		_path = working_uri;
	}
	std::string clean_path;
	if (!_decodePercentEncoding(_path, clean_path)) {
		return false;
	}
	_path = clean_path;

	return true;
}

bool	Uri::_decodePercentEncoding(const std::string& str, std::string& decoded) const {
	decoded.clear();

	for (size_t i = 0; i < str.length(); ++i) {
		if (str[i] == '%' && i + 2 < str.length()) {
			std::string hex = str.substr(i + 1, 2);

			if (hex == "2F" || hex == "2f" || hex == "00") {
				return false;
			}

			char c = static_cast<char>(std::strtol(hex.c_str(), NULL, 16));
			decoded += c;
			i += 2;
		} else {
			decoded += str[i];
		}
	}
	return true;
}

const std::string& Uri::getOriginal() const { return _original; }
const std::string& Uri::getScheme() const   { return _scheme; }
const std::string& Uri::getHost() const     { return _host; }
unsigned short     Uri::getPort() const     { return _port; }
const std::string& Uri::getPath() const     { return _path; }
const std::string& Uri::getQuery() const    { return _query; }
const std::string& Uri::getFragment() const { return _fragment; }
