#include "disk_manager.h"
#include <cstdlib>
#include <cassert>
#include <cstring>

DiskManager::DiskManager(int diskNum, int unitNum) : n(diskNum), v(unitNum) {
    // 初始化数组，索引从1开始，所以分配0号位置作为哨兵
    diskUnits.resize(n + 1);
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
        for (int i = startPos; i < startPos + size; i++) {
            diskUnits[diskId][i] = 0;  // 设为已分配但未读取
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
        for (int i = 1; i <= v && remaining > 0; i++) {
            if (diskUnits[diskId][i] == -1) {  // 空闲单元
                int startBlock = i;
                int blockSize = 0;
                
                // 寻找连续的空闲单元
                while (i <= v && diskUnits[diskId][i] == -1 && blockSize < remaining) {
                    diskUnits[diskId][i] = 0;  // 设为已分配
                    blockSize++;
                    i++;
                }
                
                result.push_back({startBlock, blockSize});
                remaining -= blockSize;
                i--;  // 回退一步，因为循环会自增
            }
        }
        
        if (remaining <= 0) {
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

std::vector<std::pair<int, int>> DiskManager::allocateOnDiskAtPosition(int diskId, int position, int size) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || position < 1 || position > v || 
        size <= 0 || position + size - 1 > v) {
        return {}; // 参数错误，返回空向量
    }
#endif
    
    // 检查指定位置的单元是否都是空闲的
    for (int i = position; i < position + size; i++) {
        if (diskUnits[diskId][i] != -1) {
            return {};  // 有非空闲单元，分配失败
        }
    }
    
    // 分配指定位置的单元
    for (int i = position; i < position + size; i++) {
        diskUnits[diskId][i] = 0;  // 设为已分配但未读取
    }
    
    // 返回分配结果
    std::vector<std::pair<int, int>> result;
    result.push_back({position, size});
    return result;
}

std::vector<std::pair<int, int>> DiskManager::allocate(int size, int& diskId) {
#ifndef NDEBUG
    if (size <= 0 || size > v) {
        diskId = -1;
        return {}; // 参数错误，返回空向量
    }
#endif
    
    // 先查找有足够空闲空间的磁盘
    for (int i = 1; i <= n; i++) {
        if (getFreeSpaceOnDisk(i) >= size) {
            auto result = allocateOnDisk(i, size);
            if (!result.empty()) {
                diskId = i;
                return result;
            }
        }
    }
    
    // 没有找到合适的磁盘
    diskId = -1;
    return {};
}

bool DiskManager::freeOnDisk(int diskId, const std::vector<std::pair<int, int>>& blocks) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || blocks.empty()) {
        return false; // 参数错误
    }
#endif
    
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
            diskUnits[diskId][i] = -1;  // 设为空闲
        }
    }
    
    return true;
}

int DiskManager::getFreeSpaceOnDisk(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n) {
        return 0; // 参数错误
    }
#endif
    
    // 计算空闲单元数量
    int freeCount = 0;
    for (int i = 1; i <= v; i++) {
        if (diskUnits[diskId][i] == -1) {
            freeCount++;
        }
    }
    
    return freeCount;
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

bool DiskManager::setBlockRead(int diskId, int position, int timeSlice) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > n || position < 1 || position > v || timeSlice < 1) {
        return false;
    }
#endif
    
    // 只有已分配的块才能标记为已读取
    if (diskUnits[diskId][position] >= 0) {
        diskUnits[diskId][position] = timeSlice;
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
    
    return diskUnits[diskId][position];
} 