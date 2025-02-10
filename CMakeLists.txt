cmake_minimum_required(VERSION 3.4...3.18)
project(pyNetX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Python components
find_package(Python COMPONENTS Interpreter Development)
if (NOT Python_FOUND)
    message(FATAL_ERROR "Cannot find Python interpreter and/or development libraries!")
endif()

# Find pybind11
find_package(pybind11 REQUIRED)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find libssh using pkg-config
pkg_check_modules(LIBSSH REQUIRED libssh)

# Find TinyXML2 using pkg-config
pkg_check_modules(TINYXML2 REQUIRED tinyxml2)

# Include directories
include_directories(
    ${Python_INCLUDE_DIRS}
    ${LIBSSH_INCLUDE_DIRS}
    ${TINYXML2_INCLUDE_DIRS}
    include
)

# Create the Python module
pybind11_add_module(pyNetX 
    src/bindings.cpp 
    src/netconf_client.cpp
)

# Link the libraries
target_link_libraries(pyNetX PRIVATE 
    ${LIBSSH_LIBRARIES} 
    ${TINYXML2_LIBRARIES}
)

install(TARGETS pyNetX
    LIBRARY DESTINATION .
    RUNTIME DESTINATION .
)