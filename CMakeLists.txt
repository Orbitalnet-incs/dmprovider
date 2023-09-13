
########################################################
# Files

SET (DTEXT_SRCS
  qgsdmfeatureiterator.cpp
  qgsdmprovider.cpp
  qgsdmfile.cpp
)

SET (DTEXT_MOC_HDRS
  qgsdmfile.h
  qgsdmprovider.h
)

IF (WITH_GUI)
  SET(DTEXT_SRCS ${DTEXT_SRCS}
    qgsdmprovidergui.cpp
    qgsdmsourceselect.cpp
  )
  SET(DTEXT_MOC_HDRS ${DTEXT_MOC_HDRS}
    qgsdmsourceselect.h
  )
ENDIF ()

########################################################
# Build

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/core/expression
  ${CMAKE_SOURCE_DIR}/src/core/geometry
  ${CMAKE_SOURCE_DIR}/src/core/layertree
  ${CMAKE_SOURCE_DIR}/src/core/metadata
  ${CMAKE_SOURCE_DIR}/src/core/raster
  ${CMAKE_SOURCE_DIR}/src/core/symbology
  ${CMAKE_SOURCE_DIR}/src/gui
  ${CMAKE_SOURCE_DIR}/external
  ${CMAKE_SOURCE_DIR}/external/nlohmann

  ${CMAKE_BINARY_DIR}/src/core
  ${CMAKE_BINARY_DIR}/src/gui
  ${CMAKE_BINARY_DIR}/src/ui
)
INCLUDE_DIRECTORIES (SYSTEM
)

QT5_WRAP_CPP(DTEXT_MOC_SRCS ${DTEXT_MOC_HDRS})

ADD_LIBRARY(dmprovider MODULE ${DTEXT_SRCS} ${DTEXT_MOC_SRCS})

TARGET_LINK_LIBRARIES(dmprovider
  qgis_core
)

IF (WITH_GUI)
  TARGET_LINK_LIBRARIES (dmprovider
    qgis_gui
  )
  ADD_DEPENDENCIES(dmprovider ui)
ENDIF ()

TARGET_COMPILE_DEFINITIONS(dmprovider PRIVATE "-DQT_NO_FOREACH")

# clang-tidy
IF(CLANG_TIDY_EXE)
  SET_TARGET_PROPERTIES(
    dmprovider PROPERTIES
    CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
  )
ENDIF(CLANG_TIDY_EXE)

########################################################
# Install

INSTALL (TARGETS dmprovider
  RUNTIME DESTINATION ${QGIS_PLUGIN_DIR}
  LIBRARY DESTINATION ${QGIS_PLUGIN_DIR})
