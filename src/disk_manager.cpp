#include "disk_manager.h"
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <algorithm>

DiskManager::DiskManager(int diskNum, int unitNum) : n(diskNum), v(unitNum) {
    // 初始化数组，索引从1开始，所以分配0号位置作为哨兵
    diskUnits.resize(n + 1);
    diskFreeSpaces.resize(n + 1, unitNum); // 初始化每个磁盘的空闲空间为unitNum
    
    for (int i = 1; i <= n; i++) {
        diskUnits[i].resize(v + 1, -1);  // 初始时所有单元都是空闲的(-1)
    }
}

DiskManager::~DiskManager() {
    // 向量会自动清理内存
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

std::vector<std::pair<int, int>> DiskManager::allocateOnDisk(int diskId, int size) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || size <= 0 || size > v) {
        return {}; // 参数错误，返回空向量
    }
#endif
    
    // 检查可用空间
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
        for (int i = 1; i <= v && remaining > 0; i++) {
            if (diskUnits[diskId][i] == -1) {  // 空闲单元
                int startBlock = i;
                int blockSize = 0;
                
                // 寻找连续的空闲单元
                while (i <= v && diskUnits[diskId][i] == -1 && blockSize < remaining) {
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
            }
        }
    }
    
    // 更新磁盘空闲空间信息
    diskFreeSpaces[diskId] += freedUnits;
    
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