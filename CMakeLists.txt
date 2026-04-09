cmake_minimum_required(VERSION 3.15...3.30)
project(pyNetX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use pybind11 with modern FindPython mode
set(PYBIND11_FINDPYTHON ON)

# Find the exact Python used for this wheel build
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

find_package(pybind11 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED libssh2)
pkg_check_modules(TINYXML2 REQUIRED tinyxml2)

pybind11_add_module(pyNetX
    src/bindings.cpp
    src/netconf_client_helpers.cpp
    src/netconf_client_common.cpp
    src/netconf_client_blocking.cpp
    src/netconf_client_non_blocking.cpp
    src/netconf_client_async.cpp
    src/netconf_client_sync.cpp
    src/thread_pool_global.cpp
    src/notification_reactor.cpp
    src/notification_reactor_manager.cpp
)

target_include_directories(pyNetX PRIVATE
    include
    ${LIBSSH2_INCLUDE_DIRS}
    ${TINYXML2_INCLUDE_DIRS}
)

target_link_libraries(pyNetX PRIVATE
    ${LIBSSH2_LIBRARIES}
    ${TINYXML2_LIBRARIES}
)

set_target_properties(pyNetX PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS pyNetX
    LIBRARY DESTINATION "pyNetX"
    RUNTIME DESTINATION "pyNetX"
)