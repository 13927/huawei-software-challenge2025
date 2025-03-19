#include "disk_manager.h"
#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <cstring>

DiskManager::DiskManager(int diskNum, int unitNum) : n(diskNum), v(unitNum) {
    // 初始化所有磁盘的空闲空间
    freeSpaces.resize(n + 1); // 使用1到n的索引，0索引不使用
    
    // 在初始状态下，每个磁盘只有一个空闲块，包含所有单元
    for (int i = 1; i <= n; i++) {
        freeSpaces[i].push_back(std::make_pair(1, v)); // 从1开始，长度为v
    }
}

DiskManager::~DiskManager() {
    // 向量会自动清理内存
}

void DiskManager::insertFreeBlock(int diskId, int start, int length) {
    if (length <= 0) return; // 忽略长度为0或负数的块
    
    auto& blocks = freeSpaces[diskId];
    
    // 按起始位置排序插入
    auto it = std::lower_bound(blocks.begin(), blocks.end(), 
                               std::make_pair(start, 0), 
                               [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                                   return a.first < b.first;
                               });
    
    blocks.insert(it, std::make_pair(start, length));
    
    // 合并相邻的空闲块
    mergeFreeBlocks(diskId);
}

void DiskManager::mergeFreeBlocks(int diskId) {
    auto& blocks = freeSpaces[diskId];
    
    if (blocks.size() <= 1) return; // 没有需要合并的块
    
    // 初始排序
    std::sort(blocks.begin(), blocks.end(), 
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.first < b.first;
              });
    
    std::vector<std::pair<int, int>> mergedBlocks;
    
    for (const auto& block : blocks) {
        if (mergedBlocks.empty()) {
            // 第一个块直接添加
            mergedBlocks.push_back(block);
        } else {
            auto& last = mergedBlocks.back();
            int lastEnd = last.first + last.second - 1; // 上一个块的结束位置
            
            if (block.first <= lastEnd + 1) {
                // 如果当前块与上一个块相邻或重叠，则合并
                int currentEnd = block.first + block.second - 1;
                int newEnd = std::max(lastEnd, currentEnd);
                last.second = newEnd - last.first + 1;
            } else {
                // 不相邻，添加为新块
                mergedBlocks.push_back(block);
            }
        }
    }
    
    blocks = std::move(mergedBlocks);
}

std::vector<std::pair<int, int>> DiskManager::allocateOnDisk(int diskId, int size) {
    if (diskId < 1 || diskId > n || size <= 0 || size > v) {
        return {}; // 参数错误，返回空向量
    }
    
    // 预先检查：判断该磁盘是否有足够的空闲空间
    int totalFreeSpace = getFreeSpaceOnDisk(diskId);
    if (totalFreeSpace < size) {
        return {}; // 空间不足，直接返回空向量
    }
    
    auto& freeBlocks = freeSpaces[diskId];
    
    // 寻找足够大的空闲块（尝试连续分配）
    for (auto it = freeBlocks.begin(); it != freeBlocks.end(); ++it) {
        if (it->second >= size) {
            // 找到足够的空间
            int start = it->first;
            int length = it->second;
            
            // 分配空间并更新空闲块
            if (length == size) {
                // 恰好使用整个块
                freeBlocks.erase(it);
            } else {
                // 只使用部分块，更新块的起始位置和大小
                it->first += size;
                it->second -= size;
            }
            
            // 创建结果向量，只有一个块
            std::vector<std::pair<int, int>> result;
            result.push_back(std::make_pair(start, size));
            
            return result;
        }
    }
    
    // 如果没有找到足够大的连续块，尝试碎片化存储
    int remainingSize = size;
    std::vector<std::pair<int, int>> result;
    
    // 按块大小降序排序，优先分配较大的块
    std::sort(freeBlocks.begin(), freeBlocks.end(), 
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.second > b.second;
              });
    
    // 逐个使用可用块，直到满足总大小要求
    auto it = freeBlocks.begin();
    while (remainingSize > 0 && it != freeBlocks.end()) {
        if (it->second > 0) {
            int start = it->first;
            int allocSize = std::min(remainingSize, it->second);
            
            // 添加到结果列表
            result.push_back(std::make_pair(start, allocSize));
            remainingSize -= allocSize;
            
            // 更新或移除使用的块
            if (it->second == allocSize) {
                // 块完全使用
                it = freeBlocks.erase(it);
            } else {
                // 部分使用块
                it->first += allocSize;
                it->second -= allocSize;
                ++it;
            }
        } else {
            ++it;
        }
    }
    
    // 如果不能满足总大小要求，回滚所有分配
    if (remainingSize > 0) {
        // 释放已分配的块
        for (const auto& block : result) {
            insertFreeBlock(diskId, block.first, block.second);
        }
        mergeFreeBlocks(diskId); // 合并可能相邻的块
        return {}; // 返回空向量表示分配失败
    }
    
    // 重新按照起始位置排序结果，使输出更易读
    std::sort(result.begin(), result.end(), 
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.first < b.first;
              });
    
    return result;
}

std::vector<std::pair<int, int>> DiskManager::allocateOnDiskAtPosition(int diskId, int position, int size) {
    if (diskId < 1 || diskId > n || position < 1 || position > v || 
        size <= 0 || position + size - 1 > v) {
        return {}; // 参数错误，返回空向量
    }
    
    // 预先检查：判断该磁盘是否有足够的总空闲空间
    int totalFreeSpace = getFreeSpaceOnDisk(diskId);
    if (totalFreeSpace < size) {
        return {}; // 空间不足，直接返回空向量
    }
    
    auto& freeBlocks = freeSpaces[diskId];
    
    // 检查请求的位置是否可用
    // 首先构建一个表示请求区域的block
    std::pair<int, int> requestedBlock = std::make_pair(position, size);
    
    // 检查是否有与请求区域重叠的已分配块
    for (auto it = freeBlocks.begin(); it != freeBlocks.end(); ) {
        // 检查两个区间是否重叠
        int freeBlockEnd = it->first + it->second - 1;
        int requestedBlockEnd = requestedBlock.first + requestedBlock.second - 1;
        
        // 如果请求块的起始位置落在当前空闲块内，并且整个请求块都在空闲块范围内
        if (requestedBlock.first >= it->first && 
            requestedBlockEnd <= freeBlockEnd) {
            
            // 从空闲块中移除请求的区域
            // 可能需要将空闲块分成两部分
            if (it->first == requestedBlock.first) {
                // 请求块起始与空闲块起始相同，只需调整空闲块起始和大小
                it->first += size;
                it->second -= size;
                
                // 如果空闲块变为空，删除它
                if (it->second <= 0) {
                    it = freeBlocks.erase(it);
                }
            } else if (freeBlockEnd == requestedBlockEnd) {
                // 请求块结尾与空闲块结尾相同，只需调整空闲块大小
                it->second = requestedBlock.first - it->first;
                ++it;
            } else {
                // 请求块在空闲块中间，需要拆分空闲块
                int leftSize = requestedBlock.first - it->first;
                int rightStart = requestedBlockEnd + 1;
                int rightSize = freeBlockEnd - requestedBlockEnd;
                
                // 更新当前块为左侧部分
                it->second = leftSize;
                ++it;
                
                // 添加右侧部分作为新的空闲块
                if (rightSize > 0) {
                    insertFreeBlock(diskId, rightStart, rightSize);
                }
            }
            
            // 创建结果向量
            std::vector<std::pair<int, int>> result;
            result.push_back(requestedBlock);
            
            // 确保空闲块列表保持正确状态
            mergeFreeBlocks(diskId);
            
            return result;
        } else {
            ++it;
        }
    }
    
    // 如果无法在指定位置分配，返回空向量
    return {};
}

std::vector<std::pair<int, int>> DiskManager::allocate(int size, int& diskId) {
    if (size <= 0 || size > v) {
        diskId = -1;
        return {}; // 参数错误，返回空向量
    }
    
    // 预先检查：先判断是否有磁盘有足够的空闲空间
    bool hasSufficientSpace = false;
    for (int i = 1; i <= n; i++) {
        if (getFreeSpaceOnDisk(i) >= size) {
            hasSufficientSpace = true;
            break;
        }
    }
    
    // 如果没有任何磁盘有足够的空闲空间，直接返回失败
    if (!hasSufficientSpace) {
        diskId = -1;
        return {};
    }
    
    // 先尝试找有足够大连续空闲块的磁盘
    for (int i = 1; i <= n; i++) {
        for (const auto& block : freeSpaces[i]) {
            if (block.second >= size) {
                std::vector<std::pair<int, int>> result = allocateOnDisk(i, size);
                if (!result.empty()) {
                    diskId = i;
                    return result;
                }
                break; // 如果分配失败，检查下一个磁盘
            }
        }
    }
    
    // 如果没有找到有足够大连续块的磁盘，尝试找总空间足够的磁盘进行碎片化存储
    for (int i = 1; i <= n; i++) {
        int totalFreeSpace = getFreeSpaceOnDisk(i);
        if (totalFreeSpace >= size) {
            // 尝试在此磁盘上进行碎片化分配
            std::vector<std::pair<int, int>> result = allocateOnDisk(i, size);
            if (!result.empty()) {
                diskId = i;
                return result;
            }
        }
    }
    
    // 所有磁盘都无法满足分配要求
    diskId = -1;
    return {}; 
}

bool DiskManager::freeOnDisk(int diskId, const std::vector<std::pair<int, int>>& blocks) {
    if (diskId < 1 || diskId > n || blocks.empty()) {
        return false; // 参数错误
    }
    
    // 检查所有块的有效性
    for (const auto& block : blocks) {
        int start = block.first;
        int length = block.second;
        
        if (start < 1 || start + length - 1 > v || length <= 0) {
            return false; // 块范围错误
        }
        
        // 将块直接插入到空闲列表中
        insertFreeBlock(diskId, start, length);
    }
    
    return true;
}

int DiskManager::getFreeSpaceOnDisk(int diskId) const {
    if (diskId < 1 || diskId > n) {
        return 0; // 参数错误
    }
    
    const auto& blocks = freeSpaces[diskId];
    int freeCount = 0;
    
    for (const auto& block : blocks) {
        freeCount += block.second;
    }
    
    return freeCount;
}

int DiskManager::getDiskCount() const {
    return n;
}

int DiskManager::getUnitCount() const {
    return v;
} 