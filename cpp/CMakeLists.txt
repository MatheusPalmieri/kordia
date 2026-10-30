cmake_minimum_required(VERSION 3.22)
project(kordia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive-)   # sources hold Portuguese text (reasons shown in the UI)
endif()

option(KORDIA_APP "Build the JUCE live app" ON)

include(FetchContent)
# header-only FFT for the core (no JUCE dependency: the lab and the tests stay light)
FetchContent_Declare(pocketfft GIT_REPOSITORY https://github.com/mreineck/pocketfft.git GIT_TAG cpp
                     GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(pocketfft)

add_library(kordia STATIC
  core/src/theory.cpp core/src/analysis.cpp core/src/tempo.cpp core/src/engine.cpp
  core/src/accomp.cpp core/src/synth.cpp core/src/processor.cpp)
target_include_directories(kordia PUBLIC core/include PRIVATE ${pocketfft_SOURCE_DIR})

# the lab: scenarios, metrics, exam, reports, session replay
add_library(kordia_lablib STATIC
  lab/rng.cpp lab/dsp.cpp lab/files.cpp lab/guitar.cpp lab/scenarios.cpp lab/metrics.cpp lab/exam.cpp lab/report.cpp)
target_include_directories(kordia_lablib PUBLIC lab PRIVATE ${pocketfft_SOURCE_DIR})
target_link_libraries(kordia_lablib PUBLIC kordia)

add_executable(kordia_lab lab/main.cpp)
target_link_libraries(kordia_lab PRIVATE kordia_lablib)

add_executable(kordia_tests tests/tests.cpp)
target_link_libraries(kordia_tests PRIVATE kordia_lablib)
target_compile_definitions(kordia_tests PRIVATE KORDIA_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(kordia_tests PRIVATE tests)

if(KORDIA_APP)
  add_subdirectory(app)
endif()
