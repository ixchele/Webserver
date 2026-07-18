#include <Response.hpp>


HttpResponse::HttpResponse(Client *client, HttpRequest *request)
    : _client(client), _request(request)
{
}

HttpResponse::~HttpResponse()
{
}

void HttpResponse::response() {
    
}

std::string HttpResponse::_get_error_message(int code) {
    switch(code) {
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default:  return "Error";
    }
}
