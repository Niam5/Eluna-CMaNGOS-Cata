/*
 * Copyright (C) 2015  MaNGOS project <http://getmangos.eu>
 * Copyright (C) 2012-2013 Arctium Emulation <http://arctium.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

namespace CMaNGOSPatcher.Patterns
{
    class Windows
    {
        public static class x86
        {
            public static byte[] A1 = { 0xF3, 0xFF, 0xFF, 0xE8, 0xB1, 0xED, 0xFF, 0xFF, 0x85, 0xC0 };
            public static byte[] A2 = { 0x83, 0xC4, 0x04, 0x84, 0xC0, 0x74, 0x23, 0x33, 0xC0, 0x8D };
            public static byte[] A3 = { 0x8B, 0x55, 0x0C, 0x83, 0xFA, 0x02, 0x75, 0x73, 0x8D, 0x45 };
        }

        public static class x64
        {
            public static byte[] B1 = { 0xD2, 0xE8, 0xA8, 0x01, 0x74, 0x1A, 0x0F, 0x1F, 0x00, 0x48 };
            public static byte[] B2 = { 0x41, 0x83, 0xFD, 0x02, 0x74, 0x08, 0x41, 0x8B, 0xD5, 0xE9 };
        }
    }
}