cmake_minimum_required(VERSION 3.24)

project(DesignRC VERSION 0.9.0 LANGUAGES CXX)

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
  src/gui/PlanViewport.cpp
  src/gui/PlanViewport.h
  src/gui/TechnicalDrawing.cpp
  src/gui/TechnicalDrawing.h
)
set_target_properties(designrc PROPERTIES AUTOMOC ON)
target_include_directories(designrc PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
target_compile_definitions(designrc PRIVATE
  DESIGNRC_VERSION="${PROJECT_VERSION}"
  DESIGNRC_RELEASE_DATE="July 19, 2026"
)
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
  execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PREFIX
    OUTPUT_VARIABLE QT_INSTALL_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_DOCS
    OUTPUT_VARIABLE QT_INSTALL_DOCS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  set(DESIGNRC_OCCT_RUNTIME_DIR "" CACHE PATH
      "Directory containing the OpenCascade runtime DLLs")
  if(NOT DESIGNRC_OCCT_RUNTIME_DIR)
    if(EXISTS "${OpenCASCADE_INSTALL_PREFIX}/win64/vc14/bin")
      set(DESIGNRC_OCCT_RUNTIME_DIR
          "${OpenCASCADE_INSTALL_PREFIX}/win64/vc14/bin")
    elseif(EXISTS "${OpenCASCADE_INSTALL_PREFIX}/win64/vc14/bind")
      set(DESIGNRC_OCCT_RUNTIME_DIR
          "${OpenCASCADE_INSTALL_PREFIX}/win64/vc14/bind")
    else()
      message(FATAL_ERROR "OpenCascade runtime DLL directory was not found")
    endif()
  endif()
  set(_occt_freetype
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/occt/3rdparty-vc14-64/freetype-2.13.3-x64/bin/freetype.dll")
  if(OpenCASCADE_WITH_FREETYPE AND NOT EXISTS "${_occt_freetype}")
    message(FATAL_ERROR "OpenCascade requires FreeType, but freetype.dll was not found at ${_occt_freetype}")
  endif()
  add_custom_command(TARGET designrc POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:designrc>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${DESIGNRC_OCCT_RUNTIME_DIR}" "$<TARGET_FILE_DIR:designrc>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_occt_freetype}" "$<TARGET_FILE_DIR:designrc>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/help" "$<TARGET_FILE_DIR:designrc>/help"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/licenses" "$<TARGET_FILE_DIR:designrc>/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
      "$<TARGET_FILE_DIR:designrc>/licenses/DESIGNRC-GPL-3.0.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
      "$<TARGET_FILE_DIR:designrc>/licenses/THIRD_PARTY_NOTICES.md"
    COMMENT "Deploying Qt and OpenCascade runtimes"
    VERBATIM
  )
  if(EXISTS "${QT_INSTALL_PREFIX}/sbom/qtbase-${Qt6_VERSION}.spdx")
    add_custom_command(TARGET designrc POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-sbom"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${QT_INSTALL_PREFIX}/sbom/qtbase-${Qt6_VERSION}.spdx"
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-sbom/qtbase-${Qt6_VERSION}.spdx"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${QT_INSTALL_PREFIX}/sbom/qtsvg-${Qt6_VERSION}.spdx"
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-sbom/qtsvg-${Qt6_VERSION}.spdx"
      COMMENT "Deploying Qt software bills of materials"
      VERBATIM
    )
  endif()
  if(EXISTS "${QT_INSTALL_DOCS}/qtdoc/licenses-used-in-qt.html")
    add_custom_command(TARGET designrc POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-attributions"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${QT_INSTALL_DOCS}/qtdoc/licenses-used-in-qt.html"
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-attributions/licenses-used-in-qt.html"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${QT_INSTALL_DOCS}/qtdoc/qt-attribution-llvmpipe.html"
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-attributions/qt-attribution-llvmpipe.html"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${QT_INSTALL_DOCS}/qtdoc/qt-attribution-llvm.html"
        "$<TARGET_FILE_DIR:designrc>/licenses/qt-attributions/qt-attribution-llvm.html"
      COMMENT "Deploying Qt and Mesa attribution documents"
      VERBATIM
    )
  endif()
endif()

if(DESIGNRC_BUILD_TESTS)
  enable_testing()
  add_executable(designrc_domain_tests tests/AirfoilProfileTests.cpp)
  target_link_libraries(designrc_domain_tests PRIVATE designrc_domain)
  add_test(NAME designrc_domain_tests COMMAND designrc_domain_tests)

  add_executable(designrc_gui_tests
    tests/GuiDefaultsTests.cpp
    src/gui/PlanViewport.cpp
    src/gui/PlanViewport.h
    src/gui/TechnicalDrawing.cpp
    src/gui/TechnicalDrawing.h
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
