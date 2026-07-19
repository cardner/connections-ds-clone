// Wi-Fi + minimal HTTP/1.1 client for fetching puzzles from the proxy.
#pragma once

namespace net {

enum class Result {
	Ok,
	WifiFail,
	DnsFail,
	ConnectFail,
	SendFail,
	HttpError,   // non-200 response
	NoBody,
	TooLarge,
};

// Human-readable description of a result.
const char *describe(Result r);

// Bring up Wi-Fi using the console's stored connection settings.
// On a DSi this uses the DSi (Atheros) driver and can join WPA2 networks.
// Blocks until connected or failed. Returns true on success.
bool wifiConnect();

// Tear down the Wi-Fi connection.
void wifiDisconnect();

// Perform an HTTP GET for http://host:port/path and copy the response body
// (null-terminated) into buf. On success returns Result::Ok and sets *outLen.
Result httpGet(const char *host, int port, const char *path, char *buf, int bufSize, int *outLen);

} // namespace net
