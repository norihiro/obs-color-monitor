cmake_minimum_required(VERSION 3.10)

# Change obs-plugintemplate to your plugin's name in a machine-readable format
# (e.g.: obs-myawesomeplugin) and set 
project(obs-color-monitor)
set(CMAKE_PROJECT_VERSION 0.3.2) # update cmake to 3.12 or later, then put to 'project'

# Replace `Your Name Here` with the name (yours or your organization's) you want
# to see as the author of the plugin (in the plugin's metadata itself and in the installers)
set(PLUGIN_AUTHOR "Norihiro Kamae")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS releases
# (used both in the installer and when submitting the installer for notarization)
set(MACOS_BUNDLEID "net.nagater.obs-color-monitor")
set(ID_PREFIX "net.nagater.obs-color-monitor.")

# Replace `me@contoso.com` with the maintainer email address you want to put in Linux packages
set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

option(ENABLE_PROFILE "Enable profiling to see calculation time" OFF)

# TAKE NOTE: No need to edit things past this point

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

set(PLUGIN_HEADERS
	src/plugin-macros.generated.h
	src/roi.h
	src/common.h
	src/obs-convenience.h
	src/scope-dock.hpp
	src/scope-dock-new-dialog.hpp
	src/scope-widget.hpp
	src/obsgui-helper.hpp
	src/scope-widget-properties.hpp
	src-obsstudio/properties-view.hpp
	src-obsstudio/vertical-scroll-area.hpp
	src-obsstudio/combobox-ignorewheel.hpp
	src-obsstudio/spinbox-ignorewheel.hpp
	src-obsstudio/double-slider.hpp
	src-obsstudio/slider-ignorewheel.hpp
	src-obsstudio/qt-wrappers.hpp
)

# --- Platform-independent build settings ---
add_library(${CMAKE_PROJECT_NAME} MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

include_directories(
	src-obsstudio/
	${Qt5Core_INCLUDES}
	${Qt5Widgets_INCLUDES}
	${Qt5Gui_INCLUDES}
	${Qt5Gui_PRIVATE_INCLUDE_DIRS}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
	OBS::libobs
	OBS::obs-frontend-api
	Qt5::Core
	Qt5::Widgets
	Qt5::Gui
	Qt5::GuiPrivate
)

# --- End of section ---

# --- Windows-specific build settings and tasks ---
if(WIN32)
	# Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL when building with VS2019)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
	endif()

	target_link_libraries(${CMAKE_PROJECT_NAME} OBS::w32-pthreads)

	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(ARCH_NAME "64bit")
		set(OBS_BUILDDIR_ARCH "build64")
	else()
		set(ARCH_NAME "32bit")
		set(OBS_BUILDDIR_ARCH "build32")
	endif()


	# --- Release package helper ---
	# The "release" folder has a structure similar OBS' one on Windows
	set(RELEASE_DIR "${PROJECT_SOURCE_DIR}/release")

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        # If config is Release or RelWithDebInfo, package release files
        COMMAND if $<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>==1 (
            "${CMAKE_COMMAND}" -E make_directory
                "${RELEASE_DIR}/data/obs-plugins/${CMAKE_PROJECT_NAME}"
                "${RELEASE_DIR}/obs-plugins/${ARCH_NAME}"
        )

        COMMAND if $<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>==1 (
            "${CMAKE_COMMAND}" -E copy_directory
                "${PROJECT_SOURCE_DIR}/data"
                "${RELEASE_DIR}/data/obs-plugins/${CMAKE_PROJECT_NAME}"
        )

        COMMAND if $<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>==1 (
            "${CMAKE_COMMAND}" -E copy
                "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                "${RELEASE_DIR}/obs-plugins/${ARCH_NAME}"
        )

        # If config is RelWithDebInfo, copy the pdb file
        COMMAND if $<CONFIG:RelWithDebInfo>==1 (
            "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>"
                "${RELEASE_DIR}/obs-plugins/${ARCH_NAME}"
        )

        # Copy to obs-studio dev environment for immediate testing
        COMMAND if $<CONFIG:Debug>==1 (
            "${CMAKE_COMMAND}" -E copy
                "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                "${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}"
        )

        COMMAND if $<CONFIG:Debug>==1 (
            "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>"
                "${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}"
        )

        COMMAND if $<CONFIG:Debug>==1 (
            "${CMAKE_COMMAND}" -E make_directory
                "${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/data/obs-plugins/${CMAKE_PROJECT_NAME}"
        )

        COMMAND if $<CONFIG:Debug>==1 (
            "${CMAKE_COMMAND}" -E copy_directory
                "${PROJECT_SOURCE_DIR}/data"
                "${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/data/obs-plugins/${CMAKE_PROJECT_NAME}"
        )
    )

endif()

if(OS_LINUX)
	target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
endif()

setup_plugin_target(${CMAKE_PROJECT_NAME})
