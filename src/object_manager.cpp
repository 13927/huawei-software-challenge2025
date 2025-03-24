#include "object_manager.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>

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
    return true;
}

// 为对象分配副本存储位置
bool ObjectManager::allocateReplicas(Object& obj) {
    int size = obj.getSize();
    std::vector<int> usedDisks; // 记录已使用的磁盘ID
    
    // 为每个副本分配空间
    for (int i = 0; i < REP_NUM; i++) {
        int selectedDiskId = -1;
        
        // 获取负载最小的N个磁盘，N为总磁盘数量
        std::vector<int> leastLoadedDisks = diskManager.getLeastLoadedDisks(3);
        
        // 从这些磁盘中排除已经用于当前对象副本的磁盘
        std::vector<int> availableDisks;
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
    
    // 释放所有副本的磁盘空间
    for (int i = 0; i < REP_NUM; i++) {
        const StorageUnit& replica = obj.getReplica(i);
        if (replica.diskId > 0) {
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
    // 优化版本：直接返回
    return (it != objects.end()) ? std::make_shared<const Object>(it->second) : nullptr;
#endif
}

// 检查对象是否存在且未被删除
bool ObjectManager::objectExists(int id) const {
    auto it = objects.find(id);
    return (it != objects.end());
} 