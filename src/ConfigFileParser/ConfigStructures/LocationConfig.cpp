#include "CommonConfig.hpp"
#include <LocationConfig.hpp>
#include <ConfigParser.hpp>
#include <sstream>

LocationConfig::LocationConfig(void) 
	: path(""), methods(GET), upload("") {
	// pass
}

void	LocationConfig::resetConf(void) {
	this->path = "";
	this->methods = GET;
	this->upload = "";
	CommonConfig::resetConf();
}


std::string LocationConfig::toString(const std::string& indent) const {
    std::stringstream ss;

    ss << indent << "--- Location [" << (path.empty() ? "UNDEFINED" : path) << "] ---\n";
    
    // Affichage des méthodes (décodage du bitwise)
    ss << indent << "methods: " << methods << " (";
    if (methods == 0) ss << "None/Default";
    else {
        if (methods & 1) ss << "GET ";
        if (methods & 2) ss << "POST ";
        if (methods & 4) ss << "DELETE";
    }
    ss << ")\n";

    ss << indent << "upload: " << (upload.empty() ? "(empty)" : upload) << "\n";

    // --- APPEL A LA CLASSE MERE ---
    // Ça va imprimer root, index, error_page, cgi_pass...
    ss << CommonConfig::str(indent);

    return ss.str();
}
