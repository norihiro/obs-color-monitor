cmake_minimum_required(VERSION 3.12)

project(obs-color-monitor VERSION 0.4.5)

set(PLUGIN_AUTHOR "Norihiro Kamae")

set(MACOS_BUNDLEID "net.nagater.obs-color-monitor")
set(ID_PREFIX "net.nagater.obs-color-monitor.")
set(MACOS_PACKAGE_UUID "0EA80C5E-945D-4A0A-A30D-593014BB5631")
set(MACOS_INSTALLER_UUID "A5C3A087-9A85-4E45-B755-C7FBE53177A9")
set(PLUGIN_URL "https://obsproject.com/forum/resources/color-monitor.1277/")

set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

option(ENABLE_PROFILE "Enable profiling to see calculation time" OFF)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# In case you need C++
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
include(cmake/ObsPluginHelpers.cmake)
find_qt(VERSION ${QT_VERSION} COMPONENTS Widgets Core Gui)

configure_file(
    src/plugin-macros.h.in
    ../src/plugin-macros.generated.h
)
configure_file(
    installer/installer-Windows.iss.in
    ../installer/installer-Windows.generated.iss
)

configure_file(
    ci/ci_includes.sh.in
    ../ci/ci_includes.generated.sh
)
configure_file(
    ci/ci_includes.cmd.in
    ../ci/ci_includes.generated.cmd
)

set(PLUGIN_SOURCES
	src/plugin-main.c
	src/vectorscope.c
	src/waveform.c
	src/histogram.c
	src/zebra.c
	src/roi.c
	src/common.c
	src/common-cpp.cc
	src/obs-convenience.c
	src/scope-dock.cpp
	src/scope-dock-new-dialog.cpp
	src/scope-widget.cpp
	src/scope-widget-properties.cpp
	src-obsstudio/properties-view.cpp
	src-obsstudio/vertical-scroll-area.cpp
	src-obsstudio/combobox-ignorewheel.cpp
	src-obsstudio/spinbox-ignorewheel.cpp
	src-obsstudio/double-slider.cpp
	src-obsstudio/slider-ignorewheel.cpp
	src-obsstudio/qt-wrappers.cpp
)

add_library(${CMAKE_PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

include_directories(
	src-obsstudio/
)

target_link_libraries(${CMAKE_PROJECT_NAME}
	OBS::libobs
	OBS::obs-frontend-api
	Qt::Core
	Qt::Widgets
	Qt::Gui
	Qt::GuiPrivate
)

if(OS_WINDOWS)
	# Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL when building with VS2019)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
	endif()

	target_link_libraries(${CMAKE_PROJECT_NAME} OBS::w32-pthreads)
endif()

if(OS_LINUX)
	target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
endif()

setup_plugin_target(${CMAKE_PROJECT_NAME})

configure_file(installer/installer-macOS.pkgproj.in installer-macOS.generated.pkgproj)
