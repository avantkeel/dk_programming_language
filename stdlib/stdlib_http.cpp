#include "dk/stdlib_http.hpp"

#include "dk/runtime.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace dk {
namespace {
constexpr std::size_t kMaxBodyBytes = 64u * 1024u * 1024u;
constexpr long kMaxRedirects = 10;

double g_timeout_seconds = 30.0;
std::string g_last_error;

void ensure_curl_initialised() {
    static const bool initialised = [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
            throw LanguageError("http: failed to initialise libcurl");
        std::atexit(curl_global_cleanup);
        return true;
    }();
    (void)initialised;
}

struct CurlDeleter { void operator()(CURL* handle) const { curl_easy_cleanup(handle); } };
struct SlistDeleter { void operator()(curl_slist* list) const { curl_slist_free_all(list); } };
using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;
using HeaderList = std::unique_ptr<curl_slist, SlistDeleter>;

struct Body {
    std::string data;
    bool too_large = false;
};

std::size_t on_body(char* chunk, std::size_t size, std::size_t count, void* user) {
    auto* body = static_cast<Body*>(user);
    const std::size_t total = size * count;
    if (body->data.size() + total > kMaxBodyBytes) {
        body->too_large = true;
        return 0;
    }
    body->data.append(chunk, total);
    return total;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::size_t on_header(char* chunk, std::size_t size, std::size_t count, void* user) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(user);
    const std::size_t total = size * count;
    const std::string line(chunk, total);
    if (line.rfind("HTTP/", 0) == 0) {
        headers->clear(); // a new response (e.g. after a redirect) replaces the old one
        return total;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) return total;
    std::string name = trim(line.substr(0, colon));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string value = trim(line.substr(colon + 1));
    auto found = headers->find(name);
    if (found == headers->end()) (*headers)[name] = value;
    else found->second += ", " + value;
    return total;
}

Value native(const std::string& name, std::size_t min, std::size_t max,
             std::function<Value(const std::vector<Value>&)> body) {
    return Value(std::make_shared<NativeFunction>(
        NativeFunction{name, min, max, std::move(body)}));
}

bool has_line_break(const std::string& text) {
    return text.find_first_of("\r\n") != std::string::npos;
}

std::string require_string(const Value& value, const std::string& what) {
    const auto* text = std::get_if<std::string>(&value.data);
    if (!text) throw LanguageError(what + " must be a string");
    return *text;
}

HeaderList build_headers(const Value& value, const std::string& fn) {
    HeaderList list;
    if (std::holds_alternative<std::monostate>(value.data)) return list;
    const auto* items = std::get_if<std::shared_ptr<ListValue>>(&value.data);
    if (!items)
        throw LanguageError(fn + " headers must be a list of \"Name: value\" strings");
    for (const auto& element : (*items)->elements) {
        const std::string header = require_string(element, fn + " header");
        if (header.find(':') == std::string::npos || has_line_break(header))
            throw LanguageError(fn + " header '" + header +
                                "' must look like \"Name: value\" on one line");
        curl_slist* extended = curl_slist_append(list.get(), header.c_str());
        if (!extended) throw LanguageError(fn + ": out of memory");
        list.release();
        list.reset(extended);
    }
    return list;
}

Value perform_request(const std::string& fn, const std::string& method,
                      const std::string& url, const std::optional<std::string>& body,
                      const Value& header_value) {
    ensure_curl_initialised();

    if (method.empty() || !std::all_of(method.begin(), method.end(), [](unsigned char c) {
            return std::isupper(c) != 0;
        }))
        throw LanguageError(fn + ": method must be upper-case letters, e.g. \"GET\"");
    if (url.empty() || has_line_break(url))
        throw LanguageError(fn + ": invalid URL");
    HeaderList request_headers = build_headers(header_value, fn);

    CurlHandle curl(curl_easy_init());
    if (!curl) throw LanguageError(fn + ": failed to create request");

    char error_buffer[CURL_ERROR_SIZE] = {0};
    Body response_body;
    std::map<std::string, std::string> response_headers;

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "dk/0.1");
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, ""); // gzip/deflate/br if built in
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, kMaxRedirects);
    // Only ever speak HTTP(S): scripts must not be able to read file:// or ftp://.
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    const long timeout_ms = static_cast<long>(g_timeout_seconds * 1000.0);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS,
                     std::min<long>(timeout_ms, 10000L));
    for (const char* variable : {"DK_CA_BUNDLE", "CURL_CA_BUNDLE", "SSL_CERT_FILE"}) {
        if (const char* path = std::getenv(variable)) {
            if (*path) {
                curl_easy_setopt(curl.get(), CURLOPT_CAINFO, path);
                break;
            }
        }
    }

    if (method == "GET") {
        curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
    } else if (method == "HEAD") {
        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);
    } else if (method == "POST") {
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    } else {
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
    }
    if (body && method != "HEAD") {
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(body->size()));
    } else if (method == "POST") {
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(0));
    }
    if (request_headers)
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, request_headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &response_headers);

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        if (response_body.too_large)
            g_last_error = "response is larger than the 64 MB limit";
        else if (error_buffer[0])
            g_last_error = error_buffer;
        else
            g_last_error = curl_easy_strerror(code);
        return Value{};
    }
    g_last_error.clear();

    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    char* effective_url = nullptr;
    curl_easy_getinfo(curl.get(), CURLINFO_EFFECTIVE_URL, &effective_url);

    auto headers = std::make_shared<StructValue>();
    headers->type_name = "headers";
    for (const auto& header : response_headers) headers->fields[header.first] = Value(header.second);

    auto response = std::make_shared<StructValue>();
    response->type_name = "Response";
    response->fields["status"] = Value(static_cast<std::int64_t>(status));
    response->fields["ok"] = Value(status >= 200 && status < 300);
    response->fields["body"] = Value(std::move(response_body.data));
    response->fields["url"] = Value(std::string(effective_url ? effective_url : url.c_str()));
    response->fields["headers"] = Value(headers);
    return Value(response);
}

Value optional_arg(const std::vector<Value>& args, std::size_t index) {
    return index < args.size() ? args[index] : Value{};
}

std::optional<std::string> body_arg(const Value& value, const std::string& fn) {
    if (std::holds_alternative<std::monostate>(value.data)) return std::nullopt;
    return require_string(value, fn + " body");
}
} // namespace

void install_http(Runtime& runtime) {
    auto http = std::make_shared<StructValue>();
    http->type_name = "http";

    // Every request function returns a Response, or null when the request could
    // not be completed at all (DNS failure, refused connection, timeout, TLS
    // error, ...). Use http.lastError() for the reason. A 404 or 500 is still a
    // normal Response: check res.status or res.ok.
    http->fields["get"] = native("http.get", 1, 2, [](const std::vector<Value>& args) {
        return perform_request("http.get", "GET", require_string(args[0], "http.get url"),
                               std::nullopt, optional_arg(args, 1));
    });
    http->fields["post"] = native("http.post", 2, 3, [](const std::vector<Value>& args) {
        return perform_request("http.post", "POST", require_string(args[0], "http.post url"),
                               body_arg(args[1], "http.post"), optional_arg(args, 2));
    });
    http->fields["request"] = native("http.request", 2, 4, [](const std::vector<Value>& args) {
        return perform_request("http.request", require_string(args[0], "http.request method"),
                               require_string(args[1], "http.request url"),
                               body_arg(optional_arg(args, 2), "http.request"),
                               optional_arg(args, 3));
    });
    http->fields["header"] = native("http.header", 2, 2, [](const std::vector<Value>& args) {
        const auto* response = std::get_if<std::shared_ptr<StructValue>>(&args[0].data);
        if (!response || (*response)->type_name != "Response")
            throw LanguageError("http.header expects a response");
        std::string name = require_string(args[1], "http.header name");
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto& headers =
            std::get<std::shared_ptr<StructValue>>((*response)->fields.at("headers").data);
        auto found = headers->fields.find(name);
        return found == headers->fields.end() ? Value{} : found->second;
    });
    http->fields["timeout"] = native("http.timeout", 1, 1, [](const std::vector<Value>& args) {
        double seconds = 0;
        if (const auto* whole = std::get_if<std::int64_t>(&args[0].data))
            seconds = static_cast<double>(*whole);
        else if (const auto* fractional = std::get_if<double>(&args[0].data))
            seconds = *fractional;
        else
            throw LanguageError("http.timeout expects a number of seconds");
        if (!(seconds > 0.0) || seconds > 3600.0)
            throw LanguageError("http.timeout must be between 0 and 3600 seconds");
        g_timeout_seconds = seconds;
        return Value{};
    });
    http->fields["lastError"] = native("http.lastError", 0, 0, [](const std::vector<Value>&) {
        return Value(g_last_error);
    });
    http->fields["encode"] = native("http.encode", 1, 1, [](const std::vector<Value>& args) {
        ensure_curl_initialised();
        const std::string text = require_string(args[0], "http.encode argument");
        CurlHandle curl(curl_easy_init());
        if (!curl) throw LanguageError("http.encode: failed to initialise libcurl");
        std::unique_ptr<char, decltype(&curl_free)> escaped(
            curl_easy_escape(curl.get(), text.c_str(), static_cast<int>(text.size())),
            &curl_free);
        if (!escaped) throw LanguageError("http.encode: out of memory");
        return Value(std::string(escaped.get()));
    });

    runtime.globals->define_builtin("http", Value(http));
}
} // namespace dk
