#pragma once
#include <cstdint>

// The launch analytics block (analytics.cpp).
// Wraps the real InternetOpenA for the image's import; returns the filter the host installs.
void *AnalyticsFilterFor(void *realInternetOpenA);
