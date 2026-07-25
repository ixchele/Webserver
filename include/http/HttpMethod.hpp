#ifndef HTTP_METHOD_HPP
#define HTTP_METHOD_HPP

enum HttpMethod {
	HTTP_UNKNOWN = 0,
	HTTP_GET     = 1,  // 1 << 0
	HTTP_POST    = 2,  // 1 << 1
	HTTP_DELETE  = 4   // 1 << 2
};

#endif
