# This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

cmake_minimum_required (VERSION 3.19)

set(EXECUTABLE_NAME "MoveMapGen")
project (${EXECUTABLE_NAME})

# disable warnings for dependencies
DisableWarnings()

ADD_DEFINITIONS(-DMMAP_GENERATOR -DNO_CORE_FUNCS)
ADD_DEFINITIONS(-DDT_POLYREF64)
ADD_DEFINITIONS(-DNO_vsnprintf)

if(UNIX)
  ADD_DEFINITIONS("-ggdb")
endif()

include_directories(
    ../../src
    ../../src/game
    ../../src/game/Vmap
)

IF(APPLE)
   FIND_LIBRARY(CORE_SERVICES CoreServices )
   SET(EXTRA_LIBS ${CORE_SERVICES})
ENDIF (APPLE)

add_library(vmaplib STATIC
    ../../src/game/Vmap/BIH.cpp
    ../../src/game/Vmap/VMapManager2.cpp
    ../../src/game/Vmap/MapTree.cpp
    ../../src/game/Vmap/TileAssembler.cpp
    ../../src/game/Vmap/WorldModel.cpp
    ../../src/game/Vmap/ModelInstance.cpp
)

target_link_libraries(vmaplib
  PUBLIC shared
  PUBLIC g3dlite
  PUBLIC detour
  PUBLIC recast
  PUBLIC ${EXTRA_LIBS}
)

target_include_directories(vmaplib
    PUBLIC "${CMAKE_SOURCE_DIR}/src/framework"
)

add_library(mmaplib STATIC
    ./src/MangosMap.h
    ./src/MMapCommon.h
    ./src/IntermediateValues.h
    ./src/IntermediateValues.cpp
    ./src/MapBuilder.h
    ./src/MapBuilder.cpp
    ./src/generator.cpp
    ./src/TerrainBuilder.cpp
    ./src/VMapExtensions.cpp
)

target_include_directories(mmaplib
    PUBLIC "${CMAKE_SOURCE_DIR}/src/framework"
)

target_link_libraries(mmaplib
  PUBLIC vmaplib
)

if (MSVC)
  set_target_properties(vmaplib PROPERTIES FOLDER "Dependencies")
  set_target_properties(mmaplib PROPERTIES FOLDER "Dependencies")
endif()

if (BUILD_EXTRACTORS)
  add_executable(${EXECUTABLE_NAME} ./src/generator.cpp ./src/IntermediateValues.h ./src/IntermediateValues.cpp ./src/MapBuilder.h ./src/MapBuilder.cpp ./src/MMapCommon.h ./src/TerrainBuilder.cpp ./src/TerrainBuilder.h ./src/VMapExtensions.cpp )

  target_link_libraries(${EXECUTABLE_NAME}
    vmaplib
    detour
    recast
    mmaplib
  )

  set(EXECUTABLE_LINK_FLAGS "")
  if(UNIX)
    if (APPLE)
      set(EXECUTABLE_LINK_FLAGS "-pthread -framework Carbon")
    else()
      set(EXECUTABLE_LINK_FLAGS "-pthread -rdynamic")
    endif()
  endif()
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS
    "${EXECUTABLE_LINK_FLAGS}"
  )

  if(MSVC)
    # Define OutDir to source/bin/(platform)_(configuaration) folder.
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DEV_BIN_DIR}/Extractors")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DEV_BIN_DIR}/Extractors")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${DEV_BIN_DIR}/Extractors")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES PROJECT_LABEL "MoveMapGen")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES FOLDER "Extractors")

    # Add script file to dev folder
    set(FILES_LIST_TO_COPY
          \"${CMAKE_SOURCE_DIR}/contrib/extractor_scripts/MoveMapGen.sh\"
          \"${CMAKE_SOURCE_DIR}/contrib/extractor_scripts/offmesh.txt\")
    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FILES_LIST_TO_COPY} \"$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>\")
  endif()

  install(TARGETS ${EXECUTABLE_NAME} DESTINATION ${BIN_DIR}/tools)
  install(FILES "${CMAKE_SOURCE_DIR}/contrib/extractor_scripts/MoveMapGen.sh" DESTINATION ${BIN_DIR}/tools)
  install(FILES "${CMAKE_SOURCE_DIR}/contrib/extractor_scripts/offmesh.txt" DESTINATION ${BIN_DIR}/tools)
endif()
