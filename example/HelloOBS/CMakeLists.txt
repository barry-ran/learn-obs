cmake_minimum_required(VERSION 3.5)

project(HelloOBS VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)

set(PROJECT_SOURCES
        main.cpp
        widget.cpp
        widget.h
        widget.ui
)
if (APPLE)
    set(PROJECT_SOURCES ${PROJECT_SOURCES}
        util.h
        util.mm)
endif()

# MACOSX_BUNDLE指定在mac下生成app bundle，而不是二进制可执行程序
if (APPLE)
    set(OS_BUNDLE MACOSX_BUNDLE)
elseif (WIN32)
    set(OS_BUNDLE WIN32)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(HelloOBS
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET HelloOBS APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(HelloOBS SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(HelloOBS ${OS_BUNDLE}
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(HelloOBS PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# obs
if (APPLE)
    set(OBS_DIR "/Applications/OBS.app/Contents")
    set(OBS_FRAMEWORKS_DIR "${OBS_DIR}/Frameworks")
    set(OBS_MACOS_DIR "${OBS_DIR}/MacOS")
    set(OBS_RESOURCES_DIR "${OBS_DIR}/Resources")
    set(OBS_PLUGINS_DIR "${OBS_DIR}/PlugIns")

    # 指定头文件目录（本项目）
    target_include_directories(HelloOBS PUBLIC ${PROJECT_SOURCE_DIR}/../../../obs-studio)
    # 复制依赖库到app目录
    file(GLOB OBS_BIN_FILES
        "${OBS_FRAMEWORKS_DIR}/libavcodec.58.dylib"
        "${OBS_FRAMEWORKS_DIR}/libavformat.58.dylib"
        "${OBS_FRAMEWORKS_DIR}/libavutil.56.dylib"
        "${OBS_FRAMEWORKS_DIR}/libavfilter.7.dylib"
        "${OBS_FRAMEWORKS_DIR}/libavdevice.58.dylib"
        "${OBS_FRAMEWORKS_DIR}/libpostproc.55.dylib"
        "${OBS_FRAMEWORKS_DIR}/libjansson.4.dylib"
        "${OBS_FRAMEWORKS_DIR}/libmbedcrypto.2.24.0.dylib"
        "${OBS_FRAMEWORKS_DIR}/libmbedtls.2.24.0.dylib"
        "${OBS_FRAMEWORKS_DIR}/libmbedx509.2.24.0.dylib"
        "${OBS_FRAMEWORKS_DIR}/libobs.0.dylib"
        "${OBS_FRAMEWORKS_DIR}/libswresample.3.dylib"
        "${OBS_FRAMEWORKS_DIR}/libswscale.5.dylib"
        "${OBS_FRAMEWORKS_DIR}/libx264.161.dylib"
        "${OBS_FRAMEWORKS_DIR}/libobs-opengl.so")
    foreach(BinFile ${OBS_BIN_FILES})
        message(STATUS "copying ${BinFile} to ${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Frameworks")
        file(COPY "${BinFile}" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Frameworks")
    endforeach()
    file(COPY "${OBS_MACOS_DIR}/libobsglad.0.dylib" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/MacOS")
    file(COPY "${OBS_MACOS_DIR}/obs-ffmpeg-mux" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/MacOS")
    
    file(COPY "${OBS_RESOURCES_DIR}/data/libobs" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Resources/data")

    file(COPY "${OBS_PLUGINS_DIR}/mac-capture.so" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/obs-plugins/")
    file(COPY "${OBS_RESOURCES_DIR}/data/obs-plugins/mac-capture" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Resources/data/obs-plugins/")
    file(COPY "${OBS_PLUGINS_DIR}/obs-x264.so" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/obs-plugins/")
    file(COPY "${OBS_RESOURCES_DIR}/data/obs-plugins/obs-x264" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Resources/data/obs-plugins/")
    file(COPY "${OBS_PLUGINS_DIR}/obs-ffmpeg.so" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/obs-plugins/")
    file(COPY "${OBS_RESOURCES_DIR}/data/obs-plugins/obs-ffmpeg" DESTINATION "${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Resources/data/obs-plugins/")

    # 指定库文件目录
    target_link_directories(HelloOBS PRIVATE ${PROJECT_BINARY_DIR}/HelloOBS.app/Contents/Frameworks)
    target_link_libraries(HelloOBS PRIVATE obs.0 "-framework AppKit")


endif()

set_target_properties(HelloOBS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HelloOBS)
endif()
