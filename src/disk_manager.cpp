#include "disk_manager.h"
#include "frequency_data.h"
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <map>
#include <iostream>
#include <fstream>

DiskManager::DiskManager(int diskNum, int unitNum, FrequencyData& freqData) 
    : n(diskNum), v(unitNum), frequencyData(freqData) {
    // 初始化数组，索引从1开始，所以分配0号位置作为哨兵
    diskUnits.resize(n + 1);
    diskFreeSpaces.resize(n + 1, unitNum); // 初始化每个磁盘的空闲空间为unitNum
    
    // 初始化标签相关的数据结构
    diskTagFreeSpaces.resize(n + 1);
    diskTagRanges.resize(n + 1);
    
    for (int i = 1; i <= n; i++) {
        diskUnits[i].resize(v + 1, -1);  // 初始时所有单元都是空闲的(-1)
        // 初始化每个磁盘的标签空闲空间数组
        diskTagFreeSpaces[i].resize(frequencyData.getTagCount() + 1, 0);
    }
    
    // 初始化预分配空间
    initializePreallocatedSpace();

    // 向文件中写入初始化预分配空间信息，不覆盖
    std::ofstream outFile("preallocated_space.txt", std::ios::app);
    if (outFile.is_open()) {
        outFile << "初始化预分配空间" << std::endl;
        for (int diskId = 1; diskId <= n; diskId++) {
            outFile << "磁盘" << diskId << "的预分配空间: ";
            for (int tag = 0; tag <= frequencyData.getTagCount(); tag++) {
                outFile << diskTagFreeSpaces[diskId][tag] << " ";
            }
            outFile << std::endl;
        }
    }

}

DiskManager::~DiskManager() {
    // 向量会自动清理内存
}

void DiskManager::initializePreallocatedSpace() {

    // 遍历每个磁盘
    for (int diskId = 1; diskId <= n; diskId++) {
        // 获取该磁盘上的所有标签分配区间
        auto diskAllocations = frequencyData.getDiskAllocation(diskId);

        // 按起始位置排序
        std::sort(diskAllocations.begin(), diskAllocations.end(),
                 [](const auto& a, const auto& b) {
                     return std::get<0>(a) < std::get<0>(b);
                 });
        
        // 存储标签区间信息
        diskTagRanges[diskId] = diskAllocations;
        
        // 初始化每个标签的空闲空间
        for (const auto& [startUnit, endUnit, tag] : diskAllocations) {
            int rangeSize = endUnit - startUnit + 1;
            diskTagFreeSpaces[diskId][tag] = rangeSize;
        }
    }
}

void DiskManager::updateTagFreeSpace(int diskId, int tag, int change) {
    if (diskId >= 1 && diskId <= n && tag >= 0 && tag < static_cast<int>(diskTagFreeSpaces[diskId].size())) {
        diskTagFreeSpaces[diskId][tag] += change;
    }
}

int DiskManager::getTagFreeSpace(int diskId, int tag) const {
    if (diskId >= 1 && diskId <= n && tag >= 0 && tag < static_cast<int>(diskTagFreeSpaces[diskId].size())) {
        return diskTagFreeSpaces[diskId][tag];
    }
    return -1;
}

std::pair<int, int> DiskManager::findConsecutiveFreeUnits(int diskId, int size) const {
    // 寻找磁盘上连续的空闲单元
    int startPos = -1;
    int consecutiveCount = 0;
    
    for (int i = 1; i <= v; i++) {
        if (diskUnits[diskId][i] == -1) {  // 空闲单元
            if (startPos == -1) {
                startPos = i;  // 记录开始位置
                consecutiveCount = 1;
            } else {
                consecutiveCount++;
            }
            
            if (consecutiveCount >= size) {
                return {startPos, consecutiveCount};  // 找到足够大的连续空间
            }
        } else {  // 非空闲单元，重置计数
            startPos = -1;
            consecutiveCount = 0;
        }
    }
    
    return {-1, 0};  // 未找到连续空间
}

// 在指定的区间内寻找连续的空闲单元
std::pair<int, int> DiskManager::findConsecutiveFreeUnits(int diskId, int size, int startUnit, int endUnit) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || size <= 0 || startUnit < 1 || endUnit > v || startUnit > endUnit) {
        return {-1, 0}; // 参数错误，返回未找到
    }
#endif

    int startPos = -1;
    int consecutiveCount = 0;
    
    for (int i = startUnit; i <= endUnit; i++) {
        if (diskUnits[diskId][i] == -1) {  // 空闲单元
            if (startPos == -1) {
                startPos = i;  // 记录开始位置
                consecutiveCount = 1;
            } else {
                consecutiveCount++;
            }
            
            if (consecutiveCount >= size) {
                return {startPos, consecutiveCount};  // 找到足够大的连续空间
            }
        } else {  // 非空闲单元，重置计数
            startPos = -1;
            consecutiveCount = 0;
        }
    }
    
    return {-1, 0};  // 未找到连续空间
}

std::vector<std::pair<int, int>> DiskManager::allocateOnDisk(int diskId, int size, int tag) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || size <= 0 || size > v) {
        return {}; // 参数错误，返回空向量
    }
#endif
    // std::cerr << "allocateOnDisk diskId: " << diskId << " size: " << size << " tag: " << tag << std::endl;
    
    // 检查指定标签的预分配空间是否足够
    int tagFreeSpace = getTagFreeSpace(diskId, tag);
    if (tagFreeSpace < size) {
        return {}; // 标签预分配空间不足
    }
    
    // 在标签预分配区间内寻找连续空闲单元
    std::vector<std::pair<int, int>> result;
    int remaining = size;
    
    // 遍历该磁盘上的标签区间
    for (const auto& [startUnit, endUnit, rangeTag] : diskTagRanges[diskId]) {
        if (rangeTag != tag) continue; // 跳过其他标签的区间
        
        // 在该区间内寻找连续空闲单元
        auto [startPos, consecutiveSize] = findConsecutiveFreeUnits(diskId, remaining, startUnit, endUnit);
        
        if (startPos != -1) {
            // 找到连续空间，分配它
            int objectIndex = 0;
            for (int i = startPos; i < startPos + consecutiveSize; i++) {
                diskUnits[diskId][i] = objectIndex++;  // 设为已分配但未读取
            }
            
            // 更新标签空闲空间
            updateTagFreeSpace(diskId, tag, -consecutiveSize);
            
            // 创建并返回分配的块
            result.push_back({startPos, consecutiveSize});
            remaining -= consecutiveSize;
            
            if (remaining <= 0) {
                break; // 已分配所有需要的空间
            }
        }
    }
    
    if (remaining <= 0) {
        // 更新磁盘空闲空间信息
        diskFreeSpaces[diskId] -= size;
        return result;  // 成功分配所有需要的空间
    } else {
        // 分配失败，恢复已分配的单元
        for (const auto& block : result) {
            for (int i = block.first; i < block.first + block.second; i++) {
                diskUnits[diskId][i] = -1;  // 恢复为空闲
            }
        }
        updateTagFreeSpace(diskId, tag, size);

        return {};  // 返回空向量表示失败
    }
}

// 重载的allocateOnDisk方法，不指定标签（默认在所有空间中分配）
std::vector<std::pair<int, int>> DiskManager::allocateOnDisk(int diskId, int size) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || size <= 0 || size > v) {
        return {}; // 参数错误，返回空向量
    }
#endif
    
    // 检查整个磁盘的可用空间是否足够
    int freeSpace = getFreeSpaceOnDisk(diskId);
    if (freeSpace < size) {
        return {}; // 空间不足
    }
    
    // 寻找连续空闲单元
    auto [startPos, consecutiveSize] = findConsecutiveFreeUnits(diskId, size);
    
    if (startPos != -1) {
        // 找到连续空间，分配它
        int objectIndex = 0;
        for (int i = startPos; i < startPos + size; i++) {
            diskUnits[diskId][i] = objectIndex++;  // 设为已分配但未读取
        }
        
        // 更新磁盘空闲空间信息
        diskFreeSpaces[diskId] -= size;
        
        // 更新受影响的标签的空闲空间
        for (int i = startPos; i < startPos + size; i++) {
            for (const auto& [startUnit, endUnit, tag] : diskTagRanges[diskId]) {
                if (i >= startUnit && i <= endUnit) {
                    // 找到了所属标签，更新该标签的空闲空间
                    updateTagFreeSpace(diskId, tag, -1);
                    break;
                }
            }
        }
        
        // 创建并返回分配的块
        std::vector<std::pair<int, int>> result;
        result.push_back({startPos, size});
        return result;
    } else {
        // 没有找到足够大的连续空间，尝试碎片化分配
        std::vector<std::pair<int, int>> result;
        int remaining = size;
        
        // 逐个分配空闲单元
        int objectIndex = 0;
        std::map<int, int> tagAllocatedUnits; // 记录每个标签分配的单元数量
        
        for (int i = 1; i <= v && remaining > 0; i++) {
            if (diskUnits[diskId][i] == -1) {  // 空闲单元
                int startBlock = i;
                int blockSize = 0;
                
                // 寻找连续的空闲单元
                while (i <= v && diskUnits[diskId][i] == -1 && blockSize < remaining) {
                    // 查找该位置属于哪个标签
                    for (const auto& [startUnit, endUnit, tag] : diskTagRanges[diskId]) {
                        if (i >= startUnit && i <= endUnit) {
                            // 找到了所属标签，更新该标签的分配计数
                            tagAllocatedUnits[tag]++;
                            break;
                        }
                    }
                    
                    diskUnits[diskId][i] = objectIndex++;  // 设为已分配
                    blockSize++;
                    i++;
                }
                
                result.push_back({startBlock, blockSize});
                remaining -= blockSize;
                i--;  // 回退一步，因为循环会自增
            }
        }
        
        if (remaining <= 0) {
            // 更新磁盘空闲空间信息
            diskFreeSpaces[diskId] -= size;
            
            // 更新各标签的空闲空间
            for (const auto& [tag, count] : tagAllocatedUnits) {
                updateTagFreeSpace(diskId, tag, -count);
            }
            
            return result;  // 成功分配所有需要的空间
        } else {
            // 分配失败，恢复已分配的单元
            for (const auto& block : result) {
                for (int i = block.first; i < block.first + block.second; i++) {
                    diskUnits[diskId][i] = -1;  // 恢复为空闲
                }
            }
            return {};  // 返回空向量表示失败
        }
    }
}

bool DiskManager::freeOnDisk(int diskId, const std::vector<std::pair<int, int>>& blocks) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || blocks.empty()) {
        return false; // 参数错误
    }
#endif
    
    int freedUnits = 0;
    std::map<int, int> tagFreedUnits; // 记录每个标签释放的单元数量
    
    // 释放指定的块
    for (const auto& block : blocks) {
        int start = block.first;
        int length = block.second;
        
#ifndef NDEBUG
        if (start < 1 || start + length - 1 > v || length <= 0) {
            return false; // 块范围错误
        }
#endif
        
        // 将块中的所有单元设为空闲
        for (int i = start; i < start + length; i++) {
            if (diskUnits[diskId][i] != -1) {
                diskUnits[diskId][i] = -1;  // 设为空闲
                freedUnits++;
                
                // 查找该位置属于哪个标签
                for (const auto& [startUnit, endUnit, tag] : diskTagRanges[diskId]) {
                    if (i >= startUnit && i <= endUnit) {
                        // 找到了所属标签，更新该标签的释放计数
                        tagFreedUnits[tag]++;
                        break;
                    }
                }
            }
        }
    }
    
    // 更新磁盘空闲空间信息
    diskFreeSpaces[diskId] += freedUnits;
    
    // 更新各标签的空闲空间
    for (const auto& [tag, count] : tagFreedUnits) {
        updateTagFreeSpace(diskId, tag, count);
    }
    
    return true;
}

int DiskManager::getFreeSpaceOnDisk(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n) {
        return 0; // 参数错误
    }
#endif
    
    return diskFreeSpaces[diskId];
}

int DiskManager::getDiskCount() const {
    return n;
}

int DiskManager::getUnitCount() const {
    return v;
}

bool DiskManager::isBlockFree(int diskId, int position) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || position < 1 || position > v) {
        return false;
    }
#endif
    
    return (diskUnits[diskId][position] == -1);
}

bool DiskManager::setBlockRead(int diskId, int position, int objectIndex) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || position < 1 || position > v) {
        return false;
    }
#endif
    
    // 只有已分配的块才能设置对象序号
    // 注意：现在我们接受objectIndex为0，因为对象序号可以从0开始
    if (diskUnits[diskId][position] >= -1) {  // -1表示空闲，>=0表示已分配
        diskUnits[diskId][position] = objectIndex;  // 设置为对象中的序号
        return true;
    }
    
    return false;
}

int DiskManager::getBlockStatus(int diskId, int position) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || position < 1 || position > v) {
        return -2;  // 参数错误，返回一个特殊值
    }
#endif
    
    // 返回磁盘块的状态
    // -1: 表示空闲
    // >=0: 表示在对象中的序号
    return diskUnits[diskId][position];
}

void DiskManager::updateDiskLoadInfo() {
    // 重新计算每个磁盘的空闲空间
    for (int i = 1; i <= n; i++) {
        int freeCount = 0;
        for (int j = 1; j <= v; j++) {
            if (diskUnits[i][j] == -1) {
                freeCount++;
            }
        }
        diskFreeSpaces[i] = freeCount;
    }
}

std::vector<int> DiskManager::getLeastLoadedDisks(int count) const {
    // 创建包含所有磁盘ID的向量
    std::vector<int> allDisks;
    for (int i = 1; i <= n; i++) {
        allDisks.push_back(i);
    }
    
    // 按照空闲空间从大到小排序（负载从小到大）
    std::sort(allDisks.begin(), allDisks.end(), [this](int a, int b) {
        return diskFreeSpaces[a] > diskFreeSpaces[b];
    });
    
    // 返回前count个磁盘ID，或者全部（如果磁盘总数小于count）
    int resultSize = std::min(count, static_cast<int>(allDisks.size()));
    return std::vector<int>(allDisks.begin(), allDisks.begin() + resultSize);
}

int DiskManager::getDiskLoad(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n) {
        return 0; // 参数错误
    }
#endif
    
    return v - diskFreeSpaces[diskId];
} 