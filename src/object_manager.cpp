#include "object_manager.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include "frequency_data.h"

// Object类构造函数
Object::Object(int id, int size, int tag) 
    : objectId(id), objectSize(size), objectTag(tag) {
    // 初始化副本，但不分配空间
    for (int i = 0; i < REP_NUM; i++) {
        replicas[i] = StorageUnit();
    }
}

// 获取指定索引的副本
const StorageUnit& Object::getReplica(int replicaIndex) const {
#ifndef NDEBUG
    if (replicaIndex < 0 || replicaIndex >= REP_NUM) {
        // 如果索引无效，返回第一个副本（或者可以抛出异常）
        return replicas[0];
    }
#endif
    return replicas[replicaIndex];
}

// 设置指定索引的副本信息
void Object::setReplica(int replicaIndex, int diskId, const std::vector<std::pair<int, int>>& blockLists) {
#ifndef NDEBUG
    if (replicaIndex < 0 || replicaIndex >= REP_NUM) {
        return; // 索引无效，不进行操作
    }
#endif
    
    replicas[replicaIndex].diskId = diskId;
    replicas[replicaIndex].blockLists = blockLists;
}

// 创建新对象
bool ObjectManager::createObject(int id, int size, int tag) {
    // 检查对象ID是否已存在
#ifndef NDEBUG
    if (objects.find(id) != objects.end()) {
        return false; // 对象ID已存在
    }
#endif
    
    // 创建对象
    Object newObject(id, size, tag);
    
    // 分配磁盘空间
    if (!allocateReplicas(newObject)) {
        return false; // 无法分配足够的空间
    }
    
    // 添加到对象映射中
    objects[id] = newObject;
    
    // 更新磁盘块到对象的映射
    for (int i = 0; i < REP_NUM; i++) {
        const StorageUnit& replica = newObject.getReplica(i);
        if (replica.diskId > 0) {
            updateBlockToObjectMapping(id, replica.diskId, replica.blockLists, true);
        }
    }
    
    return true;
}

// 为对象分配副本存储位置
bool ObjectManager::allocateReplicas(Object& obj) {
    int size = obj.getSize();
    int tag = obj.getTag();
    std::vector<int> usedDisks; // 记录已使用的磁盘ID
    
    // 为每个副本分配空间
    for (int i = 0; i < REP_NUM; i++) {
        int selectedDiskId = -1;
        std::vector<int> availableDisks;

        // 获取标签在所有磁盘上的预分配情况
        auto tagAllocations = freqData.getTagAllocation(tag);
        
        // 按照预分配的磁盘进行筛选，按负载排序
        std::vector<std::pair<int, int>> preferredDisks; // <diskId, freeSpace>
        
        for (const auto& [diskId, startUnit, endUnit] : tagAllocations) {
            // 排除已经用于当前对象副本的磁盘
            if (std::find(usedDisks.begin(), usedDisks.end(), diskId) == usedDisks.end()) {
                int freeSpace = diskManager.getFreeSpaceOnDisk(diskId);
                if (freeSpace >= size) {
                    preferredDisks.push_back({diskId, freeSpace});
                }
            }
        }
        
        // 按空闲空间从大到小排序（负载从小到大）
        std::sort(preferredDisks.begin(), preferredDisks.end(), 
                    [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // 从预分配的磁盘中选择
        for (const auto& [diskId, freeSpace] : preferredDisks) {
            std::vector<std::pair<int, int>> allocatedBlocks = diskManager.allocateOnDisk(diskId, size);
            if (!allocatedBlocks.empty()) {
                // 分配成功
                obj.setReplica(i, diskId, allocatedBlocks);
                usedDisks.push_back(diskId);
                selectedDiskId = diskId;
                break;
            }
        }
        
        
        // 如果在预分配的磁盘上分配失败，则使用原有的策略
        if (selectedDiskId == -1) {
            // 获取负载最小的磁盘
            std::vector<int> leastLoadedDisks = diskManager.getLeastLoadedDisks(diskManager.getDiskCount());
            
            // 从这些磁盘中排除已经用于当前对象副本的磁盘
            for (int diskId : leastLoadedDisks) {
                if (std::find(usedDisks.begin(), usedDisks.end(), diskId) == usedDisks.end()) {
                    availableDisks.push_back(diskId);
                }
            }
            
            // 尝试按照负载从小到大的顺序在磁盘上分配空间
            for (int diskId : availableDisks) {
                // 检查空闲空间是否足够
                if (diskManager.getFreeSpaceOnDisk(diskId) >= size) {
                    // 尝试在该磁盘上分配空间
                    std::vector<std::pair<int, int>> allocatedBlocks = diskManager.allocateOnDisk(diskId, size);
                    if (!allocatedBlocks.empty()) {
                        // 分配成功
                        obj.setReplica(i, diskId, allocatedBlocks);
                        usedDisks.push_back(diskId);
                        selectedDiskId = diskId;
                        break;
                    }
                }
            }
        }
        
        // 如果无法找到可用磁盘，回滚之前的分配并返回失败
        if (selectedDiskId == -1) {
            // 回滚之前分配的副本
            for (int j = 0; j < i; j++) {
                const StorageUnit& replica = obj.getReplica(j);
                diskManager.freeOnDisk(replica.diskId, replica.blockLists);
            }
            return false;
        }
    }
    
    return true;
}

// 删除对象
bool ObjectManager::deleteObject(int id) {
    auto it = objects.find(id);
#ifndef NDEBUG
    if (it == objects.end()) {
        return false; // 对象不存在或已被删除
    }
#endif
    
    // 获取对象
    Object& obj = it->second;
    
    // 释放所有副本的磁盘空间并更新映射
    for (int i = 0; i < REP_NUM; i++) {
        const StorageUnit& replica = obj.getReplica(i);
        if (replica.diskId > 0) {
            // 从映射中删除
            updateBlockToObjectMapping(id, replica.diskId, replica.blockLists, false);
            // 释放磁盘空间
            diskManager.freeOnDisk(replica.diskId, replica.blockLists);
        }
    }
    
    // 删除对象信息
    objects.erase(it);
    return true;
}

// 获取对象（如果对象不存在或已删除则返回nullptr）
std::shared_ptr<const Object> ObjectManager::getObject(int id) const {
    auto it = objects.find(id);
#ifndef NDEBUG
    if (it != objects.end()) {
        // 创建一个共享指针，指向存储在map中的对象
        return std::make_shared<const Object>(it->second);
    }
    return nullptr;
#else
    return (it != objects.end()) ? std::make_shared<const Object>(it->second) : nullptr;
#endif
}

// 检查对象是否存在且未被删除
bool ObjectManager::objectExists(int id) const {
    auto it = objects.find(id);
    return (it != objects.end());
}

// 更新磁盘块到对象ID的映射
void ObjectManager::updateBlockToObjectMapping(int objectId, int diskId, const std::vector<std::pair<int, int>>& blocks, bool isAdd) {
#ifndef NDEBUG
    if (diskId <= 0 || diskId >= diskBlockToObjectMap.size()) {
        return; // 无效的磁盘ID
    }
#endif
    
    // 对每个块进行处理
    for (const auto& block : blocks) {
        int startPos = block.first;
        int length = block.second;
        
        // 更新每个位置的映射
        for (int pos = startPos; pos < startPos + length; pos++) {
            if (isAdd) {
                // 添加映射
                diskBlockToObjectMap[diskId][pos] = objectId;
            } else {
                // 移除映射
                diskBlockToObjectMap[diskId].erase(pos);
            }
        }
    }
}

// 根据磁盘ID和块位置获取对象ID
int ObjectManager::getObjectIdByDiskBlock(int diskId, int blockPosition) const {
#ifndef NDEBUG
    if (diskId <= 0 || diskId >= diskBlockToObjectMap.size()) {
        return -1; // 无效的磁盘ID
    }
#endif
    
    auto& diskMap = diskBlockToObjectMap[diskId];
    auto it = diskMap.find(blockPosition);
    
    if (it != diskMap.end()) {
        return it->second; // 返回对象ID
    }
    
    return -1; // 没有找到映射的对象ID
}

// 获取指定磁盘上的所有对象ID
std::vector<int> ObjectManager::getObjectsOnDisk(int diskId) const {
#ifndef NDEBUG
    if (diskId <= 0 || diskId >= diskBlockToObjectMap.size()) {
        return {}; // 无效的磁盘ID，返回空向量
    }
#endif
    
    std::vector<int> result;
    std::unordered_map<int, bool> uniqueIds; // 用于去重
    
    const auto& diskMap = diskBlockToObjectMap[diskId];
    for (const auto& pair : diskMap) {
        int objectId = pair.second;
        if (!uniqueIds[objectId]) {
            uniqueIds[objectId] = true;
            result.push_back(objectId);
        }
    }
    
    return result;
} 