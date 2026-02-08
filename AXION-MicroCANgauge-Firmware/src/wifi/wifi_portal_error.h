#pragma once

#include <WebServer.h>

void sendErrorHtml(WebServer& server, int code, const String& title,
                   const String& msg);
