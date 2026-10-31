#pragma once

#include <functional>
#include <utility>
#include <MDS_HTTP.h>
#include <MDS_Config.h>
#include <MDS_Weather.h>
#include "MatrixDriver.h"
#include "Display.h"

// Application endpoint owner; dependencies and this object must outlive registered handlers.
class ApplicationApi {
public:
	// Borrow the HTTP server, application settings, and matrix driver.
	ApplicationApi(MDS_HTTP& http, MDS_Config& config, MatrixDriver& matrix, Display::CommandQueue& commands, MDS_Weather* weather = nullptr);
	ApplicationApi(const ApplicationApi&) = delete;
	ApplicationApi& operator=(const ApplicationApi&) = delete;

	using DisplayRouter = std::function<bool(const Display::DisplayCommand&)>;

	// Install the callback that asks Application to accept selected serial destinations.
	void setDisplayRouter(DisplayRouter router) { displayRouter_ = std::move(router); }
	static size_t encodeDisplayCommand(const Display::DisplayCommand& command, char* output, size_t capacity);
	bool receiveDisplayCommand(const uint8_t* payload, size_t length);

	// Register config, display-action, and optional weather routes once.
	void begin();

private:
	// Access the existing HTTP server used for handler registration and responses.
	MDS_HTTPServer& server() { return http_.server(); }
	void on(const char* path, HTTPMethod method, MDS_HTTPServer::THandlerFunction handler);
	bool syncDisplaySettings(bool applyCurrent = false);
	void configRequest();
	void displayRequest();
	void weatherRequest();
	void sendConfig();
	void sendError(int code, const char* message, bool applied = false);
	const MDS_ConfigSetting* findSetting(const char* key) const;
	static bool parseUnsigned(const String& text, uint32_t& value);

	MDS_HTTP& http_;
	MDS_Config& config_;
	MatrixDriver& matrix_;
	MDS_Weather* weather_;
	DisplayRouter displayRouter_;
	Display::CommandQueue& commands_;
	bool registered_ = false;
};
