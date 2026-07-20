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

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
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

if(UNIX AND NOT APPLE)
  # Use an inherited ELF RPATH so OCCT shared libraries can also resolve their
  # sibling, transitive OCCT dependencies from the custom installation.
  set_target_properties(designrc PROPERTIES
    BUILD_RPATH "${OpenCASCADE_LIBRARY_DIR}"
  )
  target_link_options(designrc PRIVATE "LINKER:--disable-new-dtags")
endif()

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
  if(UNIX AND NOT APPLE)
    set_target_properties(designrc_geometry_tests PROPERTIES
      BUILD_RPATH "${OpenCASCADE_LIBRARY_DIR}"
    )
    target_link_options(designrc_geometry_tests PRIVATE
      "LINKER:--disable-new-dtags")
  endif()
  add_test(NAME designrc_geometry_tests COMMAND designrc_geometry_tests)
endif()

if(UNIX AND NOT APPLE)
  set_target_properties(designrc PROPERTIES INSTALL_RPATH "$ORIGIN")

  install(TARGETS designrc RUNTIME DESTINATION lib/designrc)
  install(PROGRAMS packaging/linux/designrc DESTINATION bin)
  install(FILES packaging/linux/qt.conf DESTINATION lib/designrc)
  install(FILES packaging/linux/designrc.desktop
    DESTINATION share/applications)
  install(FILES packaging/linux/designrc.svg
    DESTINATION share/icons/hicolor/scalable/apps)

  install(DIRECTORY resources/help DESTINATION lib/designrc)
  install(DIRECTORY resources/licenses DESTINATION lib/designrc)
  install(FILES LICENSE
    DESTINATION lib/designrc/licenses
    RENAME DESIGNRC-GPL-3.0.txt)
  install(FILES THIRD_PARTY_NOTICES.md
    DESTINATION lib/designrc/licenses)
  install(FILES packaging/linux/copyright DESTINATION share/doc/designrc)
  find_program(GZIP_EXECUTABLE gzip REQUIRED)
  execute_process(
    COMMAND "${GZIP_EXECUTABLE}" -9 -n -c
      "${CMAKE_SOURCE_DIR}/packaging/linux/changelog"
    OUTPUT_FILE "${CMAKE_BINARY_DIR}/changelog.gz"
    COMMAND_ERROR_IS_FATAL ANY
  )
  install(FILES "${CMAKE_BINARY_DIR}/changelog.gz"
    DESTINATION share/doc/designrc)

  file(GLOB _designrc_occt_libraries
    "${OpenCASCADE_LIBRARY_DIR}/libTK*.so.8*")
  if(NOT _designrc_occt_libraries)
    message(FATAL_ERROR "No Linux OCCT runtime libraries were found")
  endif()
  install(FILES ${_designrc_occt_libraries} DESTINATION lib/designrc)
  install(DIRECTORY "${OpenCASCADE_RESOURCE_DIR}/"
    DESTINATION share/designrc/occt/resources)

  get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_LIBS
    OUTPUT_VARIABLE QT_INSTALL_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE QT_INSTALL_PLUGINS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(_designrc_qt_modules Core DBus Gui Widgets XcbQpa)
  set(_designrc_qt_libraries)
  foreach(_module IN LISTS _designrc_qt_modules)
    file(GLOB _module_libraries
      "${QT_INSTALL_LIBS}/libQt6${_module}.so.6*")
    list(APPEND _designrc_qt_libraries ${_module_libraries})
  endforeach()
  if(NOT _designrc_qt_libraries OR
      NOT EXISTS "${QT_INSTALL_PLUGINS}/platforms/libqxcb.so")
    message(FATAL_ERROR "The Linux Qt runtime or XCB platform plugin was not found")
  endif()
  install(FILES ${_designrc_qt_libraries} DESTINATION lib/designrc)
  install(FILES "${QT_INSTALL_PLUGINS}/platforms/libqxcb.so"
    DESTINATION lib/designrc/qt6/plugins/platforms)

  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_NAME designrc)
  set(CPACK_PACKAGE_VENDOR "DesignRC")
  set(CPACK_PACKAGE_CONTACT
    "Barry Foust <barryfx@users.noreply.github.com>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Parametric built-up RC aircraft wing designer")
  set(CPACK_PACKAGE_DESCRIPTION
    "DesignRC creates manufacturing geometry and technical plans for
 built-up radio-control aircraft wings. It provides an interactive 3D
 preview and exports full-scale PDF, DXF, and SVG manufacturing files.")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/barryfx/DesignRC")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
  set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER
    "Barry Foust <barryfx@users.noreply.github.com>")
  set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS /usr/lib/designrc)
  set(CPACK_STRIP_FILES ON)
  include(CPack)
endif()
