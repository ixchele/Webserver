#include <HttpStatus.hpp>
#include <Response.hpp>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <sstream>
#include <string>


HttpResponse::HttpResponse()
{
}

HttpResponse::~HttpResponse()
{
}

void HttpResponse::_delete() {
    // To do: I must check the config

    if (unlink(_request->getUri().c_str()) == -1)
    {
        switch (errno)
        {
            case ENOENT: _statusCode = HttpStatus::NotFound; break;
            case EACCES: _statusCode = HttpStatus::Forbidden; break;
            case EPERM: _statusCode = HttpStatus::Forbidden; break;
            case EISDIR: _statusCode = HttpStatus::Forbidden; break;
            default: _statusCode = HttpStatus::InternalServerError;
        }
    }
    else
    {
        _statusCode = HttpStatus::NoContent;
    }
}

void HttpResponse::response() {
    if (_request == NULL || _client == NULL)
    {
        std::cerr << "warning: response couldn't find client or request" << std::endl;
        return ;
    }
    if (_state == Building)
    {
        switch (_request->getMethod())
        {
            case HTTP_DELETE: _delete(); break; 
            // case HTTP_GET: _get(); break; 
            // case HTTP_POST: _post(); break; 
            default: break;
        }
        _build_headers();
        _state = SendingHeaders;
    }
    if (_state == SendingHeaders)
    {
        _send_headers();
        _state = SendingBody;
    }
    if (_state == SendingBody)
    {
        _state = Complete;
    }
}

void HttpResponse::_build_headers() {
    std::stringstream ss;
    ss << _statusCode;
    _buffer = "Http/1.1 " + ss.str() + _get_code_message(_statusCode) + "\r\n";
    _buffer.append("Connection: close\r\n");
}

void HttpResponse::_send_headers() {
    send(_client->get_fd(), _buffer.c_str(), _buffer.size(), 0);
}

std::string HttpResponse::_get_code_message(HttpStatus::Code code) {
    switch(code) {
        // 1xx: Informational
        case HttpStatus::Continue:                    return "Continue";
        case HttpStatus::SwitchingProtocols:          return "Switching Protocols";

        // 2xx: Successful
        case HttpStatus::OK:                          return "OK";
        case HttpStatus::Created:                     return "Created";
        case HttpStatus::Accepted:                    return "Accepted";
        case HttpStatus::NonAuthoritativeInformation: return "Non-Authoritative Information";
        case HttpStatus::NoContent:                   return "No Content";
        case HttpStatus::ResetContent:                return "Reset Content";
        case HttpStatus::PartialContent:              return "Partial Content";

        // 3xx: Redirection
        case HttpStatus::MultipleChoices:             return "Multiple Choices";
        case HttpStatus::MovedPermanently:            return "Moved Permanently";
        case HttpStatus::Found:                       return "Found";
        case HttpStatus::SeeOther:                    return "See Other";
        case HttpStatus::NotModified:                 return "Not Modified";
        case HttpStatus::UseProxy:                    return "Use Proxy";
        case HttpStatus::TemporaryRedirect:           return "Temporary Redirect";
        case HttpStatus::PermanentRedirect:           return "Permanent Redirect";

        // 4xx: Client Error
        case HttpStatus::BadRequest:                  return "Bad Request";
        case HttpStatus::Unauthorized:                return "Unauthorized";
        case HttpStatus::PaymentRequired:             return "Payment Required";
        case HttpStatus::Forbidden:                   return "Forbidden";
        case HttpStatus::NotFound:                    return "Not Found";
        case HttpStatus::MethodNotAllowed:            return "Method Not Allowed";
        case HttpStatus::NotAcceptable:               return "Not Acceptable";
        case HttpStatus::ProxyAuthenticationRequired: return "Proxy Authentication Required";
        case HttpStatus::RequestTimeout:              return "Request Timeout";
        case HttpStatus::Conflict:                    return "Conflict";
        case HttpStatus::Gone:                        return "Gone";
        case HttpStatus::LengthRequired:              return "Length Required";
        case HttpStatus::PreconditionFailed:          return "Precondition Failed";
        case HttpStatus::PayloadTooLarge:             return "Payload Too Large";
        case HttpStatus::URITooLong:                  return "URI Too Long";
        case HttpStatus::UnsupportedMediaType:        return "Unsupported Media Type";
        case HttpStatus::RangeNotSatisfiable:         return "Range Not Satisfiable";
        case HttpStatus::ExpectationFailed:           return "Expectation Failed";
        case HttpStatus::UpgradeRequired:             return "Upgrade Required";

        // 5xx: Server Error
        case HttpStatus::InternalServerError:         return "Internal Server Error";
        case HttpStatus::NotImplemented:              return "Not Implemented";
        case HttpStatus::BadGateway:                  return "Bad Gateway";
        case HttpStatus::ServiceUnavailable:          return "Service Unavailable";
        case HttpStatus::GatewayTimeout:              return "Gateway Timeout";
        case HttpStatus::HTTPVersionNotSupported:     return "HTTP Version Not Supported";

        // Fallback
        default:                                      return "Unknown Error";
    }
}
