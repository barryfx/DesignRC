cmake_minimum_required(VERSION 3.24)

project(DesignRC VERSION 0.1.0 LANGUAGES CXX)

option(DESIGNRC_BUILD_TESTS "Build DesignRC tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED OpenCASCADE_DIR)
  set(_designrc_occt "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/occt/install-debug/cmake")
  if(EXISTS "${_designrc_occt}/OpenCASCADEConfig.cmake")
    set(OpenCASCADE_DIR "${_designrc_occt}" CACHE PATH "OpenCascade package directory")
  endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(OpenCASCADE CONFIG REQUIRED)

add_library(designrc_domain
  src/domain/AirfoilProfile.cpp
  src/domain/AirfoilProfile.h
  src/domain/DxfExporter.cpp
  src/domain/DxfExporter.h
  src/domain/Point2.h
  src/domain/WingDesign.cpp
  src/domain/WingDesign.h
  src/domain/WingStructure.cpp
  src/domain/WingStructure.h
)
target_include_directories(designrc_domain PUBLIC src)
target_compile_features(designrc_domain PUBLIC cxx_std_20)

add_executable(designrc WIN32
  src/main.cpp
  src/geometry/OcctRibBuilder.cpp
  src/geometry/OcctRibBuilder.h
  src/gui/MainWindow.cpp
  src/gui/MainWindow.h
  src/gui/WingPanelEditor.cpp
  src/gui/WingPanelEditor.h
  src/gui/OcctViewport.cpp
  src/gui/OcctViewport.h
)
set_target_properties(designrc PROPERTIES AUTOMOC ON)
target_include_directories(designrc PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(designrc PRIVATE
  designrc_domain
  Qt6::Widgets
  TKernel TKMath TKService TKV3d TKOpenGl TKG2d TKG3d
  TKBRep TKTopAlgo TKPrim TKBO TKOffset TKMesh TKShHealing
)

if(MSVC)
  target_compile_options(designrc_domain PRIVATE /W4 /permissive-)
  target_compile_options(designrc PRIVATE /W4 /permissive-)
endif()

if(WIN32)
  get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

  set(_occt_runtime "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/occt/install-debug/win64/vc14/bind")
  set(_occt_freetype
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/occt/3rdparty-vc14-64/freetype-2.13.3-x64/bin/freetype.dll")
  if(OpenCASCADE_WITH_FREETYPE AND NOT EXISTS "${_occt_freetype}")
    message(FATAL_ERROR "OpenCascade requires FreeType, but freetype.dll was not found at ${_occt_freetype}")
  endif()
  add_custom_command(TARGET designrc POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:designrc>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_occt_runtime}" "$<TARGET_FILE_DIR:designrc>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_occt_freetype}" "$<TARGET_FILE_DIR:designrc>"
    COMMENT "Deploying Qt and OpenCascade runtimes"
    VERBATIM
  )
endif()

if(DESIGNRC_BUILD_TESTS)
  enable_testing()
  add_executable(designrc_domain_tests tests/AirfoilProfileTests.cpp)
  target_link_libraries(designrc_domain_tests PRIVATE designrc_domain)
  add_test(NAME designrc_domain_tests COMMAND designrc_domain_tests)

  add_executable(designrc_gui_tests
    tests/GuiDefaultsTests.cpp
    src/gui/WingPanelEditor.cpp
    src/gui/WingPanelEditor.h
  )
  set_target_properties(designrc_gui_tests PROPERTIES AUTOMOC ON)
  target_include_directories(designrc_gui_tests PRIVATE src)
  target_link_libraries(designrc_gui_tests PRIVATE designrc_domain Qt6::Widgets)
  add_test(NAME designrc_gui_tests COMMAND designrc_gui_tests)

  add_executable(designrc_geometry_tests
    tests/OcctGeometryTests.cpp
    src/geometry/OcctRibBuilder.cpp
    src/geometry/OcctRibBuilder.h
  )
  target_include_directories(designrc_geometry_tests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(designrc_geometry_tests PRIVATE
    designrc_domain TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo
    TKBRep TKTopAlgo TKPrim TKBO TKOffset TKMesh TKShHealing)
  add_test(NAME designrc_geometry_tests COMMAND designrc_geometry_tests)
endif()
