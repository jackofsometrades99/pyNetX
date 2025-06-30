cmake_minimum_required(VERSION 3.10...3.18)
project(pyNetX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# (Optional) Dummy definition for python3_add_library if not provided
if (NOT COMMAND python3_add_library)
  function(python3_add_library target)
    add_library(${target} SHARED ${ARGN})
    message(WARNING "Using dummy python3_add_library: created target ${target} with sources ${ARGN}")
  endfunction()
endif()

# Find Python3 (try including Development to get headers)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
message(STATUS "Python3_EXECUTABLE: ${Python3_EXECUTABLE}")
message(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")
if (NOT Python3_LIBRARIES)
    message(WARNING "Python3_LIBRARIES is empty—assuming Python is statically linked")
    set(Python3_LIBRARIES "")
endif()

# If Python3_INCLUDE_DIRS is still empty, set it manually.
if (NOT Python3_INCLUDE_DIRS)
  message(WARNING "Python3_INCLUDE_DIRS is empty; setting it manually")
  set(Python3_INCLUDE_DIRS "/opt/_internal/cpython-3.11.13/include/python3.11")
endif()

# Find pybind11, pkg-config, and other dependencies.
find_package(pybind11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED libssh2)
pkg_check_modules(TINYXML2 REQUIRED tinyxml2)

include_directories(
    ${Python3_INCLUDE_DIRS}
    ${LIBSSH2_INCLUDE_DIRS}
    ${TINYXML2_INCLUDE_DIRS}
    include
)

# Create the Python module using pybind11.
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

# (Optional) Set output directories.
set_target_properties(pyNetX PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_link_libraries(pyNetX PRIVATE 
    ${LIBSSH2_LIBRARIES} 
    ${TINYXML2_LIBRARIES}
)

# Install the module into a relative path.
install(TARGETS pyNetX
    LIBRARY DESTINATION "pyNetX"
    RUNTIME DESTINATION "pyNetX"
)
