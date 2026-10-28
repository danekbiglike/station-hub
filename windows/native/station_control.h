#pragma once
#include <windows.h>
#include <string>
void showStationControl(HINSTANCE instance, HWND owner, const std::wstring& host, const std::wstring& key);
int stationWidgetSkin();
void setStationWidgetSkin(int skin);
