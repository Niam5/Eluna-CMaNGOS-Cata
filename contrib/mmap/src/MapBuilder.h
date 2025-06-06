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

#ifndef _MAP_BUILDER_H
#define _MAP_BUILDER_H

#include <vector>
#include <set>
#include <map>
#include <future>
#include <sstream>

#include "TerrainBuilder.h"
#include "IntermediateValues.h"

#include "IVMapManager.h"
#include "WorldModel.h"

#include "Recast.h"
#include "DetourNavMesh.h"

using namespace std;
using namespace VMAP;
// G3D namespace typedefs conflicts with ACE typedefs

namespace MMAP
{
    // these are WORLD UNIT based metrics
    // this are basic unit dimentions
    // value have to divide GRID_SIZE(533.33333f) ( aka: 0.5333, 0.2666, 0.3333, 0.1333, etc )
    const static float BASE_UNIT_DIM = 0.2666666f;

    // All are in UNIT metrics!
    const static int VERTEX_PER_MAP = int(GRID_SIZE / BASE_UNIT_DIM + 0.5f);
    const static int VERTEX_PER_TILE = 80; // must divide VERTEX_PER_MAP
    const static int TILES_PER_MAP = VERTEX_PER_MAP / VERTEX_PER_TILE;

    class TaskQueue;

    typedef std::map<uint32, std::set<uint32>*> TileList;
    typedef std::set<uint32> MapSet;
    typedef std::unique_ptr<TaskQueue> TaskQueueUPtr;

    struct Tile
    {
        Tile() : chf(NULL), solid(NULL), cset(NULL), pmesh(NULL), dmesh(NULL) {}
        ~Tile()
        {
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            rcFreeHeightField(solid);
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
        }
        rcCompactHeightfield* chf;
        rcHeightfield* solid;
        rcContourSet* cset;
        rcPolyMesh* pmesh;
        rcPolyMeshDetail* dmesh;
    };

    class MapBuilder
    {
        public:
            MapBuilder(int threads,
                       float maxWalkableAngle   = 60.0f,
                       bool skipLiquid          = false,
                       bool skipContinents      = false,
                       bool skipJunkMaps        = true,
                       bool skipBattlegrounds   = false,
                       bool debugOutput         = false,
                       bool bigBaseUnit         = false,
                       const char* offMeshFilePath = NULL);

            ~MapBuilder();

            // if no ids provided all map will be build
            void BuildMaps(std::vector<uint32>& ids);

            // builds an mmap tile for the specified map and its mesh
            void buildSingleTile(uint32 mapID, uint32 tileX, uint32 tileY);

            bool IsMapDone(uint32 mapId) const;

        private:
            // builds list of maps, then builds all of mmap tiles (based on the skip settings)
            void buildAllMaps();

            // builds all mmap tiles for the specified map id (ignores skip settings)
            void buildMap(uint32 mapID);

            // detect maps and tiles
            void discoverTiles();
            std::set<uint32>* getTileList(uint32 mapID);

            void buildNavMesh(uint32 mapID, dtNavMesh*& navMesh);

            void buildTile(uint32 mapID, uint32 tileX, uint32 tileY, dtNavMesh* navMesh, uint32 curTile, uint32 tileCount);

            // move map building
            void buildMoveMapTile(uint32 mapID,
                                  uint32 tileX,
                                  uint32 tileY,
                                  MeshData& meshData,
                                  float bmin[3],
                                  float bmax[3],
                                  dtNavMesh* navMesh);

            void getTileBounds(uint32 tileX, uint32 tileY,
                               float* verts, int vertCount,
                               float* bmin, float* bmax);
            void getGridBounds(uint32 mapID, uint32& minX, uint32& minY, uint32& maxX, uint32& maxY);

            bool shouldSkipMap(uint32 mapID);
            bool isTransportMap(uint32 mapID);
            bool shouldSkipTile(uint32 mapID, uint32 tileX, uint32 tileY);

            TerrainBuilder* m_terrainBuilder;
            TileList m_tiles;

            bool m_debugOutput;

            const char* m_offMeshFilePath;
            bool m_skipContinents;
            bool m_skipJunkMaps;
            bool m_skipBattlegrounds;

            float m_maxWalkableAngle;
            bool m_bigBaseUnit;

            // build performance - not really used for now
            rcContext* m_rcContext;

            // Task queue that will handle all worker
            TaskQueueUPtr m_taskQueue;

            // used to know wich map have launched all its tile work
            MapSet m_mapDone;
    };

    // Task queue : not thread safe (do not add worker asynchronously)
    class TaskQueue
    {
        private:
            typedef std::pair<uint32, std::future<void>> TaskType;
            typedef std::vector<TaskType> TaskVec;

        public:
            TaskQueue(MapBuilder* mapBuilder, uint32 maxSize)
                : m_mapBuilder(mapBuilder), m_maxSize(maxSize) {
                m_taskList.reserve(maxSize);
            }
            TaskQueue() = delete;
            TaskQueue(TaskQueue const&) = delete;

            // Add worker to the queue and block until at least there is one thread free
            template<typename T>
            void PushWork(T&& work, uint32 mapId)
            {
                m_taskList.emplace_back(mapId, std::async(std::launch::async, work));
                while (m_taskList.size() == m_maxSize)
                {
                    RemoveFinishedTask();

                    // allow other thread to run
                    std::this_thread::yield();
                }
            }

            // wait all worker to finish
            void WaitAll()
            {
                while (!m_taskList.empty())
                    RemoveFinishedTask();
            }

        private:
            // Try to remove a finished worker from the queue
            void RemoveFinishedTask()
            {
                for (TaskVec::iterator fItr = m_taskList.begin(); fItr != m_taskList.end();)
                {
                    std::future_status status = fItr->second.wait_for(std::chrono::seconds(0));
                    if (status == std::future_status::ready)
                    {
                        // take a copy of current finished job work map id before deleting this iterator
                        uint32 justDeletedMapId = fItr->first;

                        fItr->second.get();
                        fItr = m_taskList.erase(fItr);

                        // check if map is done (all tile jobs should be in the queue if any remain)
                        if (m_mapBuilder->IsMapDone(justDeletedMapId))
                        {
                            MapSet onGoingMap;
                            // search if there is remaining work to do 
                            bool foundWork = false;
                            for (auto& itr : m_taskList)
                            {
                                if (itr.first == justDeletedMapId)
                                {
                                    foundWork = true;
                                    break;
                                }
                                onGoingMap.insert(itr.first);
                            }

                            if (!foundWork)
                            {
                                std::stringstream ss;
                                ss << "Map [" << justDeletedMapId << "] is done!";
                                if (onGoingMap.empty())
                                    ss << "                             \n"; // should delete some remaining char in the line
                                else
                                {
                                    ss << " Still ongoing:";
                                    for (auto mId : onGoingMap)
                                        ss << " [" << mId << "]";
                                    ss << "                              ";
                                }
                                printf("%s\n", ss.str().c_str());
                            }
                        }
                    }
                    else
                        ++fItr;
                }
            };

            MapBuilder* m_mapBuilder;       // needed for MapNuilder::IsMapDone
            uint32 m_maxSize;               // max thread usable
            TaskVec m_taskList;             // task queue in a vector
    };
}

#endif
