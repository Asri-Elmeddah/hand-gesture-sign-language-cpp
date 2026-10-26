cmake_minimum_required(VERSION 3.20)

project(HandGestureControl VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED _handgesture_configs_initialized)
    if(CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Build configurations" FORCE)
    else()
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
    set(_handgesture_configs_initialized ON CACHE INTERNAL "Configuration guard")
endif()

find_package(OpenCV 4 QUIET COMPONENTS core imgproc highgui videoio)

if(OpenCV_FOUND)
    message(STATUS "OpenCV ${OpenCV_VERSION} found via find_package (dir: ${OpenCV_DIR})")
    set(HG_LINK_TARGETS ${OpenCV_LIBS})
    set(HG_OPENCV_ROOT "${OpenCV_DIR}")
else()
    set(_hg_roots "${OpenCV_DIR}" "$ENV{OPENCV_DIR}" "F:/opencv/opencv/build" "C:/opencv/build")
    set(_hg_root "")
    foreach(_r IN LISTS _hg_roots)
        if(_r AND EXISTS "${_r}/OpenCVConfig.cmake")
            set(_hg_root "${_r}")
            break()
        endif()
    endforeach()

    if(NOT _hg_root)
        message(FATAL_ERROR
            "OpenCV 4 not found. Re-run CMake with: -DOpenCV_DIR=<path-to-opencv-build>")
    endif()

    file(GLOB _hg_world_libs "${_hg_root}/x64/*/lib/opencv_world[0-9]*.lib")
    list(FILTER _hg_world_libs EXCLUDE REGEX "[0-9]d\\.lib$")
    if(NOT _hg_world_libs)
        message(FATAL_ERROR "opencv_world import library not found under ${_hg_root}")
    endif()
    list(SORT _hg_world_libs ORDER DESCENDING)
    list(GET _hg_world_libs 0 _hg_world_lib)

    get_filename_component(_hg_lib_dir "${_hg_world_lib}" DIRECTORY)
    string(REGEX REPLACE "/lib$" "/bin" _hg_bin_dir "${_hg_lib_dir}")
    get_filename_component(_hg_dll_name "${_hg_world_lib}" NAME_WE)
    get_filename_component(_hg_include_dir "${_hg_root}/include" ABSOLUTE)

    add_library(hg_opencv_world SHARED IMPORTED GLOBAL)
    set_target_properties(hg_opencv_world PROPERTIES
        IMPORTED_IMPLIB "${_hg_world_lib}"
        IMPORTED_LOCATION "${_hg_bin_dir}/${_hg_dll_name}.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${_hg_include_dir}")

    message(STATUS "OpenCV imported manually (newer MSVC toolset): ${_hg_world_lib}")
    set(HG_LINK_TARGETS hg_opencv_world)
    set(HG_OPENCV_ROOT "${_hg_root}")
endif()

add_executable(hand_gesture_control
    src/main.cpp
    src/CameraStream.cpp
    src/HandProcessor.cpp
    src/ControlLogic.cpp
    src/EnvLoader.cpp
    src/NvidiaAIClient.cpp
    src/SignLanguageDB.cpp
    src/SpeechEngine.cpp
    src/TelemetryLogger.cpp
    src/TranscriptWriter.cpp
)

target_include_directories(hand_gesture_control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(hand_gesture_control SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(hand_gesture_control PRIVATE ${HG_LINK_TARGETS})

if(MSVC)
    target_link_libraries(hand_gesture_control PRIVATE ole32.lib sapi.lib winhttp.lib)
endif()

configure_file(cmake/update_and_build.bat.in
               ${CMAKE_CURRENT_SOURCE_DIR}/update_and_build.bat @ONLY)

if(MSVC)
    target_compile_options(hand_gesture_control PRIVATE /W4 /permissive-)
    target_compile_definitions(hand_gesture_control PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(hand_gesture_control PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    file(GLOB _opencv_bin_dirs "${HG_OPENCV_ROOT}/x64/*/bin")
    if(_opencv_bin_dirs)
        list(GET _opencv_bin_dirs 0 _opencv_bin_dir)
        file(GLOB _opencv_release_dlls "${_opencv_bin_dir}/*.dll")
        list(FILTER _opencv_release_dlls EXCLUDE REGEX "[0-9]d\\.dll$")
        foreach(_dll IN LISTS _opencv_release_dlls)
            add_custom_command(TARGET hand_gesture_control POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_dll}" "$<TARGET_FILE_DIR:hand_gesture_control>"
                VERBATIM)
        endforeach()
        message(STATUS "OpenCV runtime DLLs will be copied from: ${_opencv_bin_dir}")
    endif()
endif()
