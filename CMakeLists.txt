cmake_minimum_required(VERSION 3.22)
project(kllama VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
include(KDECMakeSettings)
include(CTest)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Network Concurrent Sql)
find_package(KF6 6.8 REQUIRED COMPONENTS I18n Kirigami)
qt_standard_project_setup(REQUIRES 6.6)
add_library(kllama_backend STATIC src/ollamaconnection.cpp src/ollamaconnection.h src/chatstore.cpp src/chatstore.h src/chatsession.cpp src/chatsession.h src/modelcapabilities.cpp src/modelcapabilities.h src/attachmentmanager.cpp src/attachmentmanager.h src/markdownrenderer.cpp src/markdownrenderer.h)
target_link_libraries(kllama_backend PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Sql Qt6::Gui KF6::I18n)
target_include_directories(kllama_backend PUBLIC src)
qt_add_executable(kllama src/main.cpp)
set_source_files_properties(src/qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
# Fedora uses shared QML plugins discovered at runtime, not static plugin imports.
qt_add_qml_module(kllama URI org.kllama.app VERSION 1.0 NO_IMPORT_SCAN QML_FILES src/qml/Main.qml)
target_link_libraries(kllama PRIVATE kllama_backend Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 KF6::Kirigami KF6::I18nQml)
install(TARGETS kllama ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES data/org.kllama.Kllama.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/org.kllama.Kllama.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES data/org.kllama.Kllama.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
ki18n_install(po)
set_source_files_properties(data/org.kllama.Kllama.svg PROPERTIES QT_RESOURCE_ALIAS kllama.svg)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(connectiontest tests/connectiontest.cpp)
    target_link_libraries(connectiontest PRIVATE kllama_backend Qt6::Test)
    add_test(NAME connection COMMAND connectiontest)
    qt_add_executable(chattest tests/chattest.cpp)
    set_target_properties(chattest PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(chattest PRIVATE kllama_backend Qt6::Test Qt6::Widgets Qt6::Quick Qt6::QuickControls2 KF6::I18nQml)
    target_compile_definitions(chattest PRIVATE KLLAMA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qt_add_resources(chattest test_icons PREFIX "/" FILES data/org.kllama.Kllama.svg)
    add_test(NAME chat COMMAND chattest)
    qt_add_executable(m3test tests/m3test.cpp)
    target_link_libraries(m3test PRIVATE kllama_backend Qt6::Test Qt6::Widgets)
    add_test(NAME m3 COMMAND m3test)
    set_tests_properties(m3 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    set_tests_properties(chat PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 45)
    add_test(NAME qml-smoke COMMAND kllama --smoke-test)
    set_tests_properties(qml-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;KLLAMA_NO_AUTOCONNECT=1;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/smoke-data;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/smoke-config" TIMEOUT 15)
endif()
qt_add_resources(kllama app_icons PREFIX "/" FILES data/org.kllama.Kllama.svg)

# Portals look up the application ID in the desktop database of the host session.
# A staged install alone is not visible there; registration is explicit and per-user.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/org.kllama.Kllama.desktop" development_desktop)
string(REPLACE "Exec=kllama" "Exec=\"$<TARGET_FILE:kllama>\"" development_desktop "${development_desktop}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/desktop/org.kllama.Kllama.desktop"
    CONTENT "${development_desktop}")
add_custom_target(register-desktop
    COMMAND "${CMAKE_COMMAND}"
        "-DDESKTOP_FILE=${CMAKE_CURRENT_BINARY_DIR}/desktop/org.kllama.Kllama.desktop"
        "-DICON_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/org.kllama.Kllama.svg"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegisterDesktop.cmake"
    DEPENDS kllama
    COMMENT "Registering this development build in the current user's desktop menu"
    VERBATIM)
