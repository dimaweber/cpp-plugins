cmake_minimum_required(VERSION 3.20)
project(plugin_demo_hardened CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Nothing is exported unless it says PLUGIN_API.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(fmt REQUIRED)
find_package(Threads REQUIRED)

add_executable(host main.cpp host_api.cpp plugin_loader.cpp)
target_include_directories(host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(host PRIVATE fmt::fmt Threads::Threads ${CMAKE_DL_LIBS})

# -rdynamic: puts the host's own symbols in the dynamic symbol table so a plugin
# can resolve api_i's vtable and typeinfo from the host image. Needed as soon as
# a plugin dynamic_casts or throws an interface type across the boundary.
set_target_properties(host PROPERTIES ENABLE_EXPORTS ON)

# MODULE, not SHARED: these are dlopen'ed, never linked against.
foreach(plugin heartbeat stale)
    add_library(${plugin} MODULE ${plugin}_plugin.cpp)
    target_include_directories(${plugin} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${plugin} PRIVATE Threads::Threads)
    set_target_properties(${plugin} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        PREFIX "lib" SUFFIX ".so")
endforeach()
