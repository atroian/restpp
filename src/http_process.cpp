/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Artur Troian
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <restpp/http_process.hpp>

#include <sstream>
#include <iostream>
#include <iomanip>

// --------------------------------------------------------------
// Implemenation of class http_request
// --------------------------------------------------------------
http_request::http_request(const std::string &host, const std::string &path, HTTP_METHOD method) :
	  m_method(method)
	, m_follow_redirects(true)
	, m_http_log(nullptr)
{

	try {
		init_curl();
	} catch (...) {
		throw;
	}

	m_uri.clear();
	m_uri.append(host);
	if (!path.empty())
		m_uri.append(path);
}

http_request::http_request(const http_request &rhs)
{
	m_uri = rhs.m_uri;
	m_method = rhs.m_method;
	m_header_params = rhs.m_header_params;
	m_query_params = rhs.m_query_params;
	m_follow_redirects = rhs.m_follow_redirects;
	m_last_request = rhs.m_last_request;
	m_upload_obj = rhs.m_upload_obj;
	m_http_log = rhs.m_http_log;

	try {
		init_curl();
	} catch (...) {
		throw;
	}

}

http_request::~http_request()
{
	curl_easy_reset(m_curl);
	curl_easy_cleanup(m_curl);
}

void http_request::init_curl()
{
	m_curl = curl_easy_init();
	if (!m_curl) {
		throw std::runtime_error("Couldn't initialize curl handle");
	}

	switch (m_method) {
	case HTTP_METHOD_GET:
		break;
	case HTTP_METHOD_PUT:
		curl_easy_setopt(m_curl, CURLOPT_PUT, 1L);
		break;
	case HTTP_METHOD_POST:
		curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
		break;
	case HTTP_METHOD_DELETE:
		curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		break;
	case HTTP_METHOD_HEAD:
		curl_easy_setopt(m_curl, CURLOPT_NOBODY, 1);
		break;
	default:
		curl_easy_cleanup(m_curl);
		throw std::runtime_error("Invalid HTTP method");
	}
}

void http_request::add_header(const std::string &key, const std::string &value)
{
	m_header_params[key] = value;
}

void http_request::del_header(const std::string &key)
{
	if (m_header_params.find(key) != m_header_params.end()) {
		m_header_params.erase(key);
	}
}

void http_request::set_headers(http_params headers)
{
	m_header_params = headers;
}

http_params http_request::get_headers() const
{
	return m_header_params;
}

void http_request::add_query(const std::string &key, const std::string &value)
{
	m_query_params[key] = value;
}

http_res http_request::perform(const std::string *body, const std::string *content_type, int timeout)
{
	http_res          ret;
	std::string       headerString;
	CURLcode          res = CURLE_OK;
	std::string       query;
	curl_slist       *headerList = NULL;

	/** Set http query if any */
	for (auto it : m_query_params) {
		if (query.empty())
			query += "?";
		else
			query += "&";

		auto encode = [&query, this](const char *str, int len) {
			char *enc = curl_easy_escape(m_curl, str, len);
			query += enc;
		};

		encode(it.first.c_str(), it.first.size());
		query += "=";
		encode(it.second.c_str(), it.second.size());
	}

	if (!query.empty()) {
		m_uri.append(query);
	}

	/** set query URL */
	curl_easy_setopt(m_curl, CURLOPT_URL, m_uri.c_str());
	/** set callback function */

//	curl_easy_setopt(m_curl, CURLOPT_HEADER, 1);

	if (m_method != HTTP_METHOD_HEAD) {
		curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, write_callback);
		/** set data object to pass to callback function */
		curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &ret);
		/** set the header callback function */
	}

	curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, header_callback);
	/** callback object for headers */
	curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, &ret);
	/** set http headers */

	if (content_type && !content_type->empty()) {
		add_header("Accept", *content_type);
	}

	try {
		for (auto it : m_header_params) {
			headerString = it.first;
			headerString += ": ";
			headerString += it.second;
			headerList = curl_slist_append(headerList, headerString.c_str());
		}
	} catch (const std::exception &e) {
		std::cerr << e.what();
		curl_slist_free_all(headerList);
		throw;
	}

	curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headerList);

	// Set body data if any
	if (body && !body->empty()) {
		if (m_method == HTTP_METHOD_PUT) {
			m_upload_obj.data = body->c_str();
			m_upload_obj.length = body->size();

			/** Now specify we want to PUT data */
			curl_easy_setopt(m_curl, CURLOPT_UPLOAD, 1L);
			/** set read callback function */
			curl_easy_setopt(m_curl, CURLOPT_READFUNCTION, read_callback);
			/** set data object to pass to callback function */
			curl_easy_setopt(m_curl, CURLOPT_READDATA, &m_upload_obj);
			/** set data size */
			curl_easy_setopt(m_curl, CURLOPT_INFILESIZE, static_cast<int64_t>(m_upload_obj.length));
		} else if (m_method == HTTP_METHOD_POST) {
			curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body->c_str());
			curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, body->size());
		}  else if (m_method == HTTP_METHOD_DELETE) {
			curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body->c_str());
		}
	}

	curl_easy_setopt(m_curl, CURLOPT_DEBUGFUNCTION, curl_trace);
	curl_easy_setopt(m_curl, CURLOPT_DEBUGDATA, this);

	if (m_http_log) {
		curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 1L);
	} else {
		curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 0L);
	}

	// set timeout
	if (timeout != 0) {
		curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, timeout);
		// dont want to get a sig alarm on timeout
		curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1);
	}

	// set follow redirect
	if (m_follow_redirects) {
		curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
	}

	res = curl_easy_perform(m_curl);

	if (res != CURLE_OK) {
		curl_slist_free_all(headerList);
		throw http_req_failure(curl_easy_strerror(res), res);
	} else {
		int64_t http_code = 0;
		curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &http_code);
		ret.code = static_cast<http_status>(http_code);

		curl_slist_free_all(headerList);
	}

	return ret;
}

size_t http_request::write_callback(void *data, size_t size, size_t nmemb, void *userdata)
{
	http_res *r;
	r = reinterpret_cast<http_res *>(userdata);
	r->body.append(reinterpret_cast<char *>(data), size * nmemb);

	return (size * nmemb);
}

size_t http_request::header_callback(void *data, size_t size, size_t nmemb, void *userdata)
{
	http_res *r;
	r = reinterpret_cast<http_res *>(userdata);
	std::string header(reinterpret_cast<char *>(data), size * nmemb);
	size_t seperator = header.find_first_of(":");
	if ( std::string::npos == seperator ) {
		// roll with non seperated headers...
		trim(header);
		if (0 == header.length()) {
			return (size * nmemb);  // blank line;
		}
		r->headers[header] = "present";
	} else {
		std::string key = header.substr(0, seperator);
		trim(key);
		std::string value = header.substr(seperator + 1);
		trim(value);
		r->headers[key] = value;
	}

	return (size * nmemb);
}

size_t http_request::read_callback(void *data, size_t size, size_t nmemb, void *userdata)
{
	/** get upload struct */
	http_upload_object *u;
	u = reinterpret_cast<http_upload_object *>(userdata);

	/** set correct sizes */
	size_t curl_size = size * nmemb;
	size_t copy_size = (u->length < curl_size) ? u->length : curl_size;
	/** copy data to buffer */
	std::memcpy(data, u->data, copy_size);
	/** decrement length and increment data pointer */
	u->length -= copy_size;
	u->data += copy_size;
	/** return copied size */
	return copy_size;
}

int http_request::curl_trace(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	(void)handle;

	http_request *obj = reinterpret_cast<http_request *>(userp);

	if (obj->m_http_log) {
		const char *text;

		switch (type) {
		case CURLINFO_HEADER_OUT:
			text = "=> Send header";
			break;
		case CURLINFO_DATA_OUT:
			text = "=> Send data";
			break;
		case CURLINFO_SSL_DATA_OUT:
			text = "=> Send SSL data";
			break;
		case CURLINFO_HEADER_IN:
			text = "<= Recv header";
			break;
		case CURLINFO_DATA_IN:
			text = "<= Recv data";
			break;
		case CURLINFO_SSL_DATA_IN:
			text = "<= Recv SSL data";
			break;
		case CURLINFO_TEXT: {
			std::stringstream stream;
			stream << "== Info: " << data;
			obj->m_http_log(stream);
		}
		default: /* in case a new one is introduced to shock us */
			return 0;
		}

		obj->curl_dump(text, (uint8_t *) data, size);
	}

	return 0;
}

void http_request::curl_dump(const char *text, uint8_t *ptr, size_t size)
{
	std::stringstream stream;
	char nohex = 1;
	unsigned int width=0xFFF;

	//if(nohex)
	/* without the hex output, we can fit more on screen */
//	width = 0xFFF;

	stream
		<< text
		<< ", "
		<< std::setw(10) << std::to_string(size)
		<< " bytes "
		<< "(0x"
		<< std::hex
		<< size << ")"
		<< std::endl;

	for (size_t i = 0; i < size; i += width) {
		stream << "   ";

		if (!nohex) {
			/* hex not disabled, show it */
			/* show hex to the left */
			for (size_t c = 0; c < width; c++) {
				if (i + c < size)
					stream << std::setw(2) << std::hex << ptr[i + c];
				else
					stream << "   ";
			}
		}

		/* show data on the right */
		for(size_t c = 0; (c < width) && (i + c < size); c++) {
			/* check for 0D0A; if found, skip past and start a new line of output */
			if(nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D && ptr[i + c + 1] == 0x0A) {
				i += (c + 2 - width);
				break;
			}

			char ch = (ptr[i + c] >= 0x20) && (ptr[i + c] < 0x80) ? ptr[i + c] : '.';

			stream << ch;

			/* check again for 0D0A, to avoid an extra \n if it's at width */
			if(nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D && ptr[i + c + 2] == 0x0A) {
				i += (c + 3 - width);
				break;
			}
		}
		stream << std::endl;
	}

	m_http_log(stream);
}

// --------------------------------------------------------------
// Implemenation of class http_req_base
// --------------------------------------------------------------
http_req_base::http_req_base(const std::string &host, const std::string &path, HTTP_METHOD method) :
	  http_request(host, path, method)
	, timestamp_(timestamp_str_ms())
{ }

http_req_base::http_req_base(const http_req_base &rhs) :
	  http_request(rhs)
{
	m_data = rhs.m_data;
	content_type_ = rhs.content_type_;
}

http_req_base::~http_req_base()
{
//	jwt_.reset();
}

http_res http_req_base::perform(int timeout)
{
	http_res response;

	try {
		response = http_request::perform(m_data.get(), &content_type_, timeout);
	} catch (...) {
		throw;
	}

	return response;
}

// --------------------------------------------------------------
// Implemenation of class http_req_get
// --------------------------------------------------------------
http_req_get::http_req_get(const std::string &host, const std::string &path) :
	http_req_base(host, path, HTTP_METHOD_GET)
{

}

http_req_get::~http_req_get()
{

}

// --------------------------------------------------------------
// Implemenation of class http_req_post
// --------------------------------------------------------------
http_req_post::http_req_post(const std::string &host, const std::string &path, const std::string &data) :
	http_req_base(host, path, HTTP_METHOD_POST)
{
	m_data = std::make_shared<std::string>(data);
}

http_req_post::~http_req_post()
{

}

// --------------------------------------------------------------
// Implemenation of class http_req_put
// --------------------------------------------------------------
http_req_put::http_req_put(const std::string &host, const std::string &path, const std::string &data) :
	http_req_base(host, path, HTTP_METHOD_PUT)
{
	m_data = std::make_shared<std::string>(data);
}

http_req_put::~http_req_put()
{

}

// --------------------------------------------------------------
// Implemenation of class http_req_del
// --------------------------------------------------------------
http_req_del::http_req_del(const std::string &host, const std::string &path) :
	http_req_base(host, path, HTTP_METHOD_DELETE)
{
}

http_req_del::http_req_del(const std::string &host, const std::string &path, const std::string &data) :
	http_req_base(host, path, HTTP_METHOD_DELETE)
{
	m_data = std::make_shared<std::string>(data);
}

http_req_del::~http_req_del()
{

}

