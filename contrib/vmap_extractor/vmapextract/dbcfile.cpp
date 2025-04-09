/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "dbcfile.h"

DBCFile::DBCFile(HANDLE mpq, const char* filename) :
    mpq(mpq), filename(filename), file(NULL), data(NULL), stringTable(NULL)
{
}

bool DBCFile::open()
{
    if (!SFileOpenFileEx(mpq, filename, SFILE_OPEN_FROM_MPQ, &file))
        return false;

    char header[4];
    unsigned int na, nb, es, ss;

    DWORD readBytes = 0;
    SFileReadFile(file, header, 4, &readBytes, NULL);
    if (readBytes != 4)                                         // Number of records
        return false;

    if (header[0] != 'W' || header[1] != 'D' || header[2] != 'B' || header[3] != 'C')
        return false;

    readBytes = 0;
    SFileReadFile(file, &na, 4, &readBytes, NULL);
    if (readBytes != 4)                                         // Number of records
        return false;

    readBytes = 0;
    SFileReadFile(file, &nb, 4, &readBytes, NULL);
    if (readBytes != 4)                                         // Number of fields
        return false;

    readBytes = 0;
    SFileReadFile(file, &es, 4, &readBytes, NULL);
    if (readBytes != 4)                                         // Size of a record
        return false;

    readBytes = 0;
    SFileReadFile(file, &ss, 4, &readBytes, NULL);
    if (readBytes != 4)                                         // String size
        return false;

    recordSize = es;
    recordCount = na;
    fieldCount = nb;
    stringSize = ss;
    assert(fieldCount * 4 != recordSize);

    data = new unsigned char[recordSize * recordCount + stringSize];
    stringTable = data + recordSize * recordCount;

    size_t data_size = recordSize * recordCount + stringSize;
    readBytes = 0;
    SFileReadFile(file, data, data_size, &readBytes, NULL);
    if (readBytes != data_size)
        return false;

    return true;
}

DBCFile::~DBCFile()
{
    delete [] data;
    if (file != NULL)
        SFileCloseFile(file);
}

DBCFile::Record DBCFile::getRecord(size_t id)
{
    assert(data);
    return Record(*this, data + id * recordSize);
}

size_t DBCFile::getMaxId()
{
    assert(data);

    size_t maxId = 0;
    for (size_t i = 0; i < getRecordCount(); ++i)
        if (maxId < getRecord(i).getUInt(0))
            maxId = getRecord(i).getUInt(0);

    return maxId;
}

DBCFile::Iterator DBCFile::begin()
{
    assert(data);
    return Iterator(*this, data);
}

DBCFile::Iterator DBCFile::end()
{
    assert(data);
    return Iterator(*this, stringTable);
}

