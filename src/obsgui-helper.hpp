#pragma once
#include <QWindow>
#if !defined(_WIN32) && !defined(__APPLE__) // if Linux
#include <obs-nix-platform.h>
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// copied from obs-studio/frontend/widgets/OBSQTDisplay.cpp
static inline bool QTToGSWindow(QWindow *window, gs_window &gswindow)
{
	bool success = true;

#ifdef _WIN32
	gswindow.hwnd = (HWND)window->winId();
#elif __APPLE__
	gswindow.view = (id)window->winId();
#else
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		gswindow.id = window->winId();
		gswindow.display = obs_get_nix_platform_display();
		break;
#ifdef ENABLE_WAYLAND
	case OBS_NIX_PLATFORM_WAYLAND: {
		QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
		gswindow.display = native->nativeResourceForWindow("surface", window);
		success = gswindow.display != nullptr;
		break;
	}
#endif
	default:
		success = false;
		break;
	}
#endif
	return success;
}

// copied from obs-studio/UI/display-helpers.hpp
static inline QSize GetPixelSize(QWidget *widget)
{
	return widget->size() * widget->devicePixelRatioF();
}
