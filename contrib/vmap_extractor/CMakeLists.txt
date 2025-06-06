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
project (MANGOS_VMAP_EXTRACT_IO)

# uncomment next line to disable debug mode
ADD_DEFINITIONS("-DIOMAP_DEBUG")
# build setup currently only supports libmpq 0.4.x
ADD_DEFINITIONS("-DUSE_LIBMPQ04")

if(UNIX)
  ADD_DEFINITIONS("-ggdb")
endif()

if (APPLE)
  add_definitions("-Dfopen64=fopen")
  add_definitions("-Dfseeko64=fseeko")
  add_definitions("-Dfseek64=fseek")
  add_definitions("-Dftell64=ftell")
  add_definitions("-Dftello64=ftello")
endif()

add_subdirectory(vmapextract)
