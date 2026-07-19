#include "net.hpp"

#include <nds.h>
#include <dswifi9.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

namespace {

// Decode HTTP/1.1 "Transfer-Encoding: chunked" bodies in place.
int dechunk(char *body, int len) {
	char *src = body;
	char *end = body + len;
	char *dst = body;
	while (src < end) {
		char *nl = (char *)memchr(src, '\n', end - src);
		if (!nl) break;
		long chunkLen = strtol(src, nullptr, 16);
		src = nl + 1;
		if (chunkLen <= 0) break;
		if (src + chunkLen > end) chunkLen = end - src;
		memmove(dst, src, chunkLen);
		dst += chunkLen;
		src += chunkLen;
		if (src < end && *src == '\r') src++;
		if (src < end && *src == '\n') src++;
	}
	*dst = 0;
	return (int)(dst - body);
}

// Case-insensitive substring search within [buf, buf+len).
bool containsCI(const char *buf, int len, const char *needle) {
	int nlen = (int)strlen(needle);
	for (int i = 0; i + nlen <= len; i++) {
		int j = 0;
		for (; j < nlen; j++) {
			if (tolower((unsigned char)buf[i + j]) != tolower((unsigned char)needle[j]))
				break;
		}
		if (j == nlen) return true;
	}
	return false;
}

} // namespace

const char *net::describe(Result r) {
	switch (r) {
		case Result::Ok:          return "OK";
		case Result::WifiFail:    return "Wi-Fi connect failed";
		case Result::DnsFail:     return "DNS lookup failed";
		case Result::ConnectFail: return "connection failed";
		case Result::SendFail:    return "send failed";
		case Result::HttpError:   return "server error";
		case Result::NoBody:      return "empty response";
		case Result::TooLarge:    return "response too large";
	}
	return "unknown error";
}

bool net::wifiConnect() {
	// WFC_CONNECT: use stored access points and block until associated.
	return Wifi_InitDefault(WFC_CONNECT);
}

void net::wifiDisconnect() {
	// dswifi v2 (calico) exposes only Wifi_DisconnectAP() for teardown.
	Wifi_DisconnectAP();
}

net::Result net::httpGet(const char *host, int port, const char *path,
                         char *buf, int bufSize, int *outLen) {
	if (outLen) *outLen = 0;

	struct hostent *he = gethostbyname(host);
	if (!he || !he->h_addr_list || !he->h_addr_list[0])
		return Result::DnsFail;

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return Result::ConnectFail;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u16)port);
	memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		closesocket(sock);
		return Result::ConnectFail;
	}

	char req[256];
	int reqLen = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: ConnectionsDS/1.0\r\n"
		"Accept: text/plain\r\n"
		"Connection: close\r\n"
		"\r\n",
		path, host);

	int sent = 0;
	while (sent < reqLen) {
		int w = send(sock, req + sent, reqLen - sent, 0);
		if (w <= 0) {
			closesocket(sock);
			return Result::SendFail;
		}
		sent += w;
	}

	int total = 0;
	bool overflow = false;
	while (total < bufSize - 1) {
		int r = recv(sock, buf + total, bufSize - 1 - total, 0);
		if (r <= 0) break;
		total += r;
	}
	if (total >= bufSize - 1)
		overflow = true;
	buf[total] = 0;

	shutdown(sock, 0);
	closesocket(sock);

	if (total <= 0)
		return Result::NoBody;

	// Parse status line: "HTTP/1.x <code> ...".
	if (strncmp(buf, "HTTP/1.", 7) == 0) {
		const char *sp = strchr(buf, ' ');
		int code = sp ? atoi(sp + 1) : 0;
		if (code != 200)
			return Result::HttpError;
	}

	// Split headers from body.
	char *sep = strstr(buf, "\r\n\r\n");
	if (!sep)
		return Result::NoBody;
	int headerLen = (int)(sep - buf);
	char *body = sep + 4;
	int bodyLen = total - (int)(body - buf);
	if (bodyLen <= 0)
		return Result::NoBody;

	bool chunked = containsCI(buf, headerLen, "transfer-encoding: chunked");

	// Compact the body to the front of the buffer.
	memmove(buf, body, bodyLen);
	buf[bodyLen] = 0;

	if (chunked)
		bodyLen = dechunk(buf, bodyLen);

	if (outLen) *outLen = bodyLen;
	if (overflow)
		return Result::TooLarge;
	return Result::Ok;
}
