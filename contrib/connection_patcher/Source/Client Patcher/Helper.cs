﻿/*
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

using System;
using System.IO;
using CMaNGOSPatcher.Constants;

namespace CMaNGOSPatcher
{
    class Helper
    {
        public static BinaryTypes GetBinaryType(byte[] data)
        {
            BinaryTypes type = 0u;

            using (var reader = new BinaryReader(new MemoryStream(data)))
            {
                var magic = (uint)reader.ReadUInt16();

                // Check MS-DOS magic
                if (magic == 0x5A4D)
                {
                    reader.BaseStream.Seek(0x3C, SeekOrigin.Begin);

                    // Read PE start offset
                    var peOffset = reader.ReadUInt32();

                    reader.BaseStream.Seek(peOffset, SeekOrigin.Begin);

                    var peMagic = reader.ReadUInt32();

                    // Check PE magic
                    if (peMagic != 0x4550)
                        throw new NotSupportedException("Not a PE file!");

                    type = (BinaryTypes)reader.ReadUInt16();
                }
                else
                {
                    reader.BaseStream.Seek(0, SeekOrigin.Begin);

                    type = (BinaryTypes)reader.ReadUInt32();
                }
            }

            return type;
        }
    }
}
