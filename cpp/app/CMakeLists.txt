# Kordia live app (JUCE 8): WASAPI audio, the core from ../core, the web UI from acompanhante/ui in a WebView2.
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# WebView2 SDK (NuGet package) for the UI; downloaded once into the build tree
set(WEBVIEW2_VERSION 1.0.2210.55)
set(WEBVIEW2_DIR ${CMAKE_BINARY_DIR}/webview2/Microsoft.Web.WebView2.${WEBVIEW2_VERSION})
if(NOT EXISTS ${WEBVIEW2_DIR}/build/native/include/WebView2.h)
  file(DOWNLOAD https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}
       ${CMAKE_BINARY_DIR}/webview2.nupkg STATUS dl)
  file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/webview2.nupkg DESTINATION ${WEBVIEW2_DIR})
endif()
set(JUCE_WEBVIEW2_PACKAGE_LOCATION ${CMAKE_BINARY_DIR}/webview2 CACHE PATH "" FORCE)

juce_add_gui_app(KordiaApp
  PRODUCT_NAME "Kordia"
  COMPANY_NAME "Kordia"
  VERSION ${PROJECT_VERSION}
  NEEDS_WEBVIEW2 TRUE)

juce_add_binary_data(KordiaUI SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../acompanhante/ui/index.html)

target_sources(KordiaApp PRIVATE Main.cpp LiveEngine.cpp)
target_compile_definitions(KordiaApp PRIVATE
  JUCE_WEB_BROWSER=1
  JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
  JUCE_USE_CURL=0
  JUCE_WASAPI=1
  JUCE_DIRECTSOUND=1
  JUCE_ASIO=0                      # needs the Steinberg SDK (licence); WASAPI low-latency covers Windows for now
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:KordiaApp,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:KordiaApp,JUCE_VERSION>"
  KORDIA_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")   # sessions go to <repo>/sessoes, reports use <repo>/.venv
target_link_libraries(KordiaApp PRIVATE
  kordia KordiaUI
  juce::juce_gui_extra juce::juce_audio_utils juce::juce_audio_formats
  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# headless smoke test of the live path (real devices, volume 0)
juce_add_console_app(KordiaLiveTest PRODUCT_NAME "KordiaLiveTest")
target_sources(KordiaLiveTest PRIVATE LiveTest.cpp LiveEngine.cpp)
target_compile_definitions(KordiaLiveTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_WASAPI=1 JUCE_ASIO=0
  KORDIA_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(KordiaLiveTest PRIVATE kordia juce::juce_audio_utils juce::juce_audio_formats
  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
