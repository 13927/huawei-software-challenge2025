#ifndef OBJECT_MANAGER_H
#define OBJECT_MANAGER_H

// TODO: 
// 1. allocateReplicas 需要修改
// 2. deleteObject 未来要配合磁头任务
// 3. 

#include <vector>
#include <unordered_map>
#include <memory>
#include "disk_manager.h"
#include "constants.h"

// 表示磁盘上的存储单元
struct StorageUnit {
    int diskId;                      // 存储的磁盘ID
    std::vector<std::pair<int, int>> blockLists; // 存储的块（起始位置，长度）

    StorageUnit() : diskId(0) {}
    StorageUnit(int id) : diskId(id) {}
};

// 对象类，包含对象的基本信息和存储位置
class Object {
private:
    int objectId;                     // 对象ID
    int objectSize;                   // 对象大小（所需单元数）
    int objectTag;                    // 对象标签
    StorageUnit replicas[REP_NUM];    // 对象的三个副本存储位置
    // bool isDeleted;                   // 对象是否已删除

public:
    Object() : objectId(0), objectSize(0), objectTag(0) {}
    Object(int id, int size, int tag);

    // 获取对象信息
    int getId() const { return objectId; }
    int getSize() const { return objectSize; }
    int getTag() const { return objectTag; }

    // 副本管理
    const StorageUnit& getReplica(int replicaIndex) const;
    // 
    void setReplica(int replicaIndex, int diskId, const std::vector<std::pair<int, int>>& blocks);
};

// 对象管理器类，管理所有对象
class ObjectManager {
private:
    std::unordered_map<int, Object> objects;  // 对象ID到对象的映射
    DiskManager& diskManager;                 // 磁盘管理器引用

    // 为对象分配副本存储位置
    bool allocateReplicas(Object& obj);

public:
    ObjectManager(DiskManager& dm) : diskManager(dm) {}
    
    // 创建新对象
    bool createObject(int id, int size, int tag);
    
    // 删除对象
    bool deleteObject(int id);
    
    // 获取对象
    std::shared_ptr<const Object> getObject(int id) const;
    
    // 检查对象是否存在
    bool objectExists(int id) const;
};

#endif // OBJECT_MANAGER_H 