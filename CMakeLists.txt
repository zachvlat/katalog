cmake_minimum_required(VERSION 3.24)
project(qutestore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent Network)
find_package(KF6Kirigami2 6.0 QUIET)
find_package(KF6I18n 6.0 QUIET)
find_package(KF6Notifications 6.0 QUIET)

if (NOT KF6Notifications_FOUND)
    find_package(KF6Notifications5 QUIET)
endif()

qt_add_executable(qutestore
    src/main.cpp
    src/flatpakinstance.cpp
    src/entrymodel.cpp
    src/backend.cpp
    src/details.cpp
    src/transaction.cpp
    src/flathubclient.cpp
    src/remotemodel.cpp
    src/leftovermodel.cpp
)

# Optional Flatpak/AppStream support. On the host these come from the system;
# inside the Flatpak bundle libflatpak is built into the app prefix (/app) while
# AppStream and GLib are provided by the runtime. The search hints below cover
# both layouts (including Debian-style multiarch directories).
set(_qs_arch "${CMAKE_LIBRARY_ARCHITECTURE}")
set(_qs_include_hints
    /app/include
    /usr/include
    /usr/local/include
    /run/host/usr/include
    /run/host/usr/local/include)
set(_qs_library_hints
    /app/lib
    /app/lib64
    /usr/lib
    /usr/lib64
    /usr/local/lib
    /run/host/usr/lib
    /run/host/usr/lib64
    /run/host/usr/local/lib)
if(_qs_arch)
    list(APPEND _qs_include_hints /usr/include/${_qs_arch} /run/host/usr/include/${_qs_arch})
    list(APPEND _qs_library_hints
        /app/lib/${_qs_arch}
        /usr/lib/${_qs_arch}
        /usr/local/lib/${_qs_arch}
        /run/host/usr/lib/${_qs_arch}
        /run/host/usr/local/lib/${_qs_arch})
endif()

find_path(GLIB_INCLUDE_DIR glib.h HINTS ${_qs_include_hints} PATH_SUFFIXES glib-2.0)
find_path(GLIB_CONFIG_INCLUDE_DIR glibconfig.h HINTS ${_qs_library_hints}
          PATH_SUFFIXES glib-2.0/include glib-2.0)
find_library(GLIB_LIBRARY NAMES glib-2.0 HINTS ${_qs_library_hints})
find_library(GOBJECT_LIBRARY NAMES gobject-2.0 HINTS ${_qs_library_hints})
find_library(GIO_LIBRARY NAMES gio-2.0 HINTS ${_qs_library_hints})

find_path(FLATPAK_INCLUDE_DIR flatpak/flatpak.h HINTS ${_qs_include_hints})
find_library(FLATPAK_LIBRARY NAMES flatpak HINTS /app/lib /app/lib64 ${_qs_library_hints})
if(FLATPAK_INCLUDE_DIR AND FLATPAK_LIBRARY)
    target_compile_definitions(qutestore PRIVATE HAVE_FLATPAK=1)
    target_include_directories(qutestore PRIVATE
        ${FLATPAK_INCLUDE_DIR}
        ${FLATPAK_INCLUDE_DIR}/flatpak
    )
    target_link_libraries(qutestore PRIVATE ${FLATPAK_LIBRARY})
endif()

find_path(APPSTREAM_INCLUDE_DIR appstream.h HINTS ${_qs_include_hints} PATH_SUFFIXES appstream)
find_library(APPSTREAM_LIBRARY NAMES appstream HINTS ${_qs_library_hints})
if(APPSTREAM_INCLUDE_DIR AND APPSTREAM_LIBRARY)
    target_compile_definitions(qutestore PRIVATE HAVE_APPSTREAM=1)
    target_include_directories(qutestore PRIVATE
        ${APPSTREAM_INCLUDE_DIR}
        ${APPSTREAM_INCLUDE_DIR}/appstream
    )
    target_link_libraries(qutestore PRIVATE ${APPSTREAM_LIBRARY})
endif()

if(GLIB_INCLUDE_DIR AND GLIB_LIBRARY)
    target_include_directories(qutestore PRIVATE ${GLIB_INCLUDE_DIR})
    if(GLIB_CONFIG_INCLUDE_DIR)
        target_include_directories(qutestore PRIVATE ${GLIB_CONFIG_INCLUDE_DIR})
    endif()
    target_link_libraries(qutestore PRIVATE ${GLIB_LIBRARY})
endif()
if(KF6I18N_FOUND)
    target_link_libraries(qutestore PRIVATE KF6::I18n)
endif()
if(KF6Notifications_FOUND OR KF6NOTIFICATIONS_FOUND OR KF6Notifications5_FOUND)
    target_link_libraries(qutestore PRIVATE KF6::Notifications)
    target_compile_definitions(qutestore PRIVATE HAVE_KNOTIFICATIONS)
endif()
if(GOBJECT_LIBRARY)
    target_link_libraries(qutestore PRIVATE ${GOBJECT_LIBRARY})
endif()
if(GIO_LIBRARY)
    target_link_libraries(qutestore PRIVATE ${GIO_LIBRARY})
endif()

qt_add_qml_module(qutestore
    URI com.zachvlat.qutestore
    VERSION 1.0
    SOURCES
        src/flathubclient.cpp
        src/remotemodel.cpp
        src/leftovermodel.cpp
    QML_FILES
        qml/Main.qml
        qml/AppDetailPage.qml
        qml/RepositoriesPage.qml
        qml/LeftoverPage.qml
    NO_RESOURCE_TARGET_PATH
)

target_include_directories(qutestore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(qutestore PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
    Qt6::Network
)

install(TARGETS qutestore RUNTIME DESTINATION bin)
install(FILES data/com.zachvlat.qutestore.desktop DESTINATION share/applications)
install(FILES data/com.zachvlat.qutestore.metainfo.xml DESTINATION share/metainfo)

# App icon, named after the app id, as both a scalable SVG and the raster
# sizes the icon theme and software centres look for.
install(FILES data/icons/hicolor/scalable/apps/com.zachvlat.qutestore.svg
        DESTINATION share/icons/hicolor/scalable/apps)
foreach(_size 64 128 256 512)
    install(FILES data/icons/hicolor/${_size}x${_size}/apps/com.zachvlat.qutestore.png
            DESTINATION share/icons/hicolor/${_size}x${_size}/apps)
endforeach()
