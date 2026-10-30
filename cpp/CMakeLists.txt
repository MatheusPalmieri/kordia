cmake_minimum_required(VERSION 3.22)
project(kordia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive-)   # sources hold Portuguese text (reasons shown in the UI)
endif()

option(KORDIA_PYTHON "Build the kordia_core Python module (lab harness runs the C++ core)" ON)
option(KORDIA_APP "Build the JUCE live app" ON)

include(FetchContent)
# header-only FFT for the core (no JUCE dependency, so the Python module stays light)
FetchContent_Declare(pocketfft GIT_REPOSITORY https://github.com/mreineck/pocketfft.git GIT_TAG cpp
                     GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(pocketfft)

add_library(kordia STATIC
  core/src/theory.cpp core/src/analysis.cpp core/src/tempo.cpp core/src/engine.cpp
  core/src/accomp.cpp core/src/synth.cpp core/src/processor.cpp)
target_include_directories(kordia PUBLIC core/include PRIVATE ${pocketfft_SOURCE_DIR})
set_target_properties(kordia PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(KORDIA_PYTHON)
  find_package(Python 3.11 COMPONENTS Interpreter Development.Module REQUIRED)
  FetchContent_Declare(pybind11 GIT_REPOSITORY https://github.com/pybind/pybind11.git GIT_TAG v3.0.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(pybind11)
  pybind11_add_module(kordia_core_py python/bindings.cpp)
  target_link_libraries(kordia_core_py PRIVATE kordia)
  # lands next to the Python package: `from acompanhante import kordia_core`
  set_target_properties(kordia_core_py PROPERTIES OUTPUT_NAME kordia_core
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../acompanhante
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/../acompanhante
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/../acompanhante)
endif()

if(KORDIA_APP)
  add_subdirectory(app)
endif()
