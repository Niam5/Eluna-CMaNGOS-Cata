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

cmake_minimum_required (VERSION 3.19)
set(EXECUTABLE_NAME "ad")

list(APPEND AD_SOURCE
     ${CMAKE_SOURCE_DIR}/dep/loadlib/loadlib.cpp
	 ${CMAKE_SOURCE_DIR}/dep/loadlib/adt.cpp
	 ${CMAKE_SOURCE_DIR}/dep/loadlib/wdt.cpp
     dbcfile.cpp
     System.cpp
)

include_directories(${CMAKE_SOURCE_DIR}/dep/loadlib)

add_executable(${EXECUTABLE_NAME} ${AD_SOURCE})

target_link_libraries(${EXECUTABLE_NAME} storm)

include_directories(
    ${CMAKE_SOURCE_DIR}/dep/StormLib/src
	${CMAKE_SOURCE_DIR}/dep/loadlib
)

if(MSVC)
  # Define OutDir to source/bin/(platform)_(configuaration) folder.
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DEV_BIN_DIR}/Extractors")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DEV_BIN_DIR}/Extractors")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${DEV_BIN_DIR}/Extractors")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES PROJECT_LABEL "MapDbcExtractor")
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES FOLDER "Extractors")
endif()

install(TARGETS ${EXECUTABLE_NAME} DESTINATION ${BIN_DIR}/tools)
