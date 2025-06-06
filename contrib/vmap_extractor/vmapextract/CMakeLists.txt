# This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

set(EXECUTABLE_NAME "vmap_extractor")
project (${EXECUTABLE_NAME})

include_directories(
    ${CMAKE_SOURCE_DIR}/dep/StormLib/src
)

file(GLOB vmap_extract_src
     "*.cpp"
)

add_executable(${EXECUTABLE_NAME} ${vmap_extract_src})

target_link_libraries(${EXECUTABLE_NAME} storm)

if(MSVC)
  # Define OutDir to source/bin/(platform)_(configuaration) folder.
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DEV_BIN_DIR}/Extractors")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DEV_BIN_DIR}/Extractors")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${DEV_BIN_DIR}/Extractors")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES PROJECT_LABEL "VMapExtractor")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES FOLDER "Extractors")
  
  # Add script file to dev folder
  add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different \"${CMAKE_SOURCE_DIR}/contrib/extractor_scripts/ExtractResources.sh\" \"$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>\")
endif()

install(FILES "${CMAKE_SOURCE_DIR}/contrib/extractor_scripts/ExtractResources.sh" DESTINATION ${BIN_DIR}/tools)
install(TARGETS ${EXECUTABLE_NAME} DESTINATION ${BIN_DIR}/tools)
