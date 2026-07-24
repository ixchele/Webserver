#ifndef URI_HPP
#define URI_HPP

#include <string>

class Uri {
public:
    Uri();
    Uri(const std::string& raw_uri);
    ~Uri();

    bool parse(const std::string& raw_uri);
    void clear();

    const std::string&  getOriginal() const;
    const std::string&  getScheme() const;
    const std::string&  getHost() const;
    unsigned short      getPort() const;
    const std::string&  getPath() const;
    const std::string&  getQuery() const;
    const std::string&  getFragment() const;

private:
    std::string     _original;
    std::string     _scheme;
    std::string     _host;
    unsigned short  _port;
    std::string     _path;
    std::string     _query;
    std::string     _fragment;

    bool	_decodePercentEncoding(const std::string& str, std::string &decoded) const;
};

#endif
