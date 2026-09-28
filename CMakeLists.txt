# obs-irl-control - Copyright (C) 2026 Anikeen UG (haftungsbeschränkt) & Co. KG
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16...3.30)

project(obs-irl-control VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

option(LINUX_PORTABLE "Install into ~/.config/obs-studio/plugins instead of the system prefix" OFF)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(CURL REQUIRED)

add_library(${PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/irl-config.cpp
  src/stats-server.cpp
  src/health-check.cpp
  src/irl-controller.cpp
  src/irl-dock.cpp
  src/settings-dialog.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  PLUGIN_NAME="${PROJECT_NAME}"
  PLUGIN_VERSION="${PROJECT_VERSION}"
  QT_NO_KEYWORDS
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  OBS::libobs
  OBS::obs-frontend-api
  Qt6::Core
  Qt6::Widgets
  CURL::libcurl
)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(WIN32)
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION obs-plugins/64bit RUNTIME DESTINATION obs-plugins/64bit)
  install(DIRECTORY data/ DESTINATION data/obs-plugins/${PROJECT_NAME})
elseif(APPLE)
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${PROJECT_NAME}.plugin/Contents/MacOS)
  install(DIRECTORY data/ DESTINATION ${PROJECT_NAME}.plugin/Contents/Resources)
elseif(LINUX_PORTABLE)
  # Per-user plugin directory. Inside the OBS Flatpak sandbox XDG_CONFIG_HOME points at
  # ~/.var/app/com.obsproject.Studio/config, which is exactly where Flatpak OBS looks.
  if(DEFINED ENV{XDG_CONFIG_HOME} AND NOT "$ENV{XDG_CONFIG_HOME}" STREQUAL "")
    set(_config_home "$ENV{XDG_CONFIG_HOME}")
  else()
    set(_config_home "$ENV{HOME}/.config")
  endif()
  set(_portable "${_config_home}/obs-studio/plugins/${PROJECT_NAME}")
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION "${_portable}/bin/64bit")
  install(DIRECTORY data/ DESTINATION "${_portable}/data")
else()
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins)
  install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${PROJECT_NAME})
endif()
