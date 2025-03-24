#include "read_request_manager.h"
#include <iostream>
#include <climits> // 用于INT_MAX
#include "constants.h" // 用于REP_NUM

ReadRequestManager::ReadRequestManager(ObjectManager& objMgr, DiskHeadManager& diskMgr)
    : objectManager(objMgr), diskHeadManager(diskMgr) {

    // diskLoadCounter.resize(diskHeadManager.getDiskCount() + 1, 0); // 假设最大磁盘ID为99
}

bool ReadRequestManager::addReadRequest(int requestId, int objectId) {
    // 获取对象信息
    auto obj = objectManager.getObject(objectId);

#ifndef NDEBUG
    // 检查请求ID是否已存在
    if (requests.find(requestId) != requests.end()) {
        std::cout << "警告: 请求ID " << requestId << " 已存在" << std::endl;
        return false;
    }
    
    // 检查对象是否存在
    if (!obj) {
        std::cout << "错误: 对象ID " << objectId << " 不存在" << std::endl;
        return false;
    }
#endif
    
    // 创建新的读取请求
    ReadRequest request(requestId, objectId);
    request.status = REQUEST_PENDING;

    // 更新总剩余单元数
    request.totalRemainingUnits = obj->getSize();

    // 保存请求
    requests[requestId] = request;
    pendingRequests.push_back(requestId);
    
    // 更新对象到请求的映射
    objectToRequests[objectId].insert(requestId);

    return true;
}

bool ReadRequestManager::allocateReadRequests() {
    // 检查是否有等待处理的请求
    if (pendingRequests.empty()) {
        return false;
    }

    // 将所有等待处理的请求设置为处理中
    while (!pendingRequests.empty()) {
        // 获取一个等待处理的请求
        int requestId = pendingRequests.back();
        pendingRequests.pop_back();
        
        auto requestIt = requests.find(requestId);
#ifndef NDEBUG
        if (requestIt == requests.end()) {
            continue; // 跳过不存在的请求
        }
#endif
        
        ReadRequest& request = requestIt->second;
        
        // 获取对象信息
        auto obj = objectManager.getObject(request.objectId);
#ifndef NDEBUG
        if (!obj) {
            // 对象已不存在，移除此请求
            requests.erase(requestId);
            continue;
        }
#endif
        
        // 初始化请求的磁盘单元信息
        if (request.remainingUnits.empty()) {
            // 找到离磁头最近的副本
            int closestReplicaIndex = -1;
            int closestDistance = INT_MAX;
            
            // 检查每个副本
            for (int replicaIndex = 0; replicaIndex < REP_NUM; replicaIndex++) {
                const StorageUnit& replica = obj->getReplica(replicaIndex);
                int diskId = replica.diskId;
                
                // 获取该磁盘的磁头位置
                int headPosition = diskHeadManager.getHeadPosition(diskId);
                
                // 计算该副本的首个存储单元到磁头的最小距离
                int minDistance = INT_MAX;
                for (const auto& blockPair : replica.blockLists) {
                    int startPos = blockPair.first;
                    
                    // 计算距离（考虑环形结构）
                    int distance = (startPos - headPosition)>0 ? (startPos - headPosition) : (diskHeadManager.getDiskManager().getUnitCount() - headPosition + startPos);
                    int diskSize = diskHeadManager.getDiskManager().getUnitCount();
                    int alternativeDistance = diskSize - distance;
                    int finalDistance = std::min(distance, alternativeDistance);
                    
                    minDistance = std::min(minDistance, finalDistance);
                }
                
                // 如果此副本距离更近，更新最近副本索引
                if (minDistance < closestDistance) {
                    closestDistance = minDistance;
                    closestReplicaIndex = replicaIndex;
                }
            }
            
            // 如果找不到有效副本，使用第一个副本（不应该发生）
            if (closestReplicaIndex == -1) {
                closestReplicaIndex = 0;
            }
            
            // 使用最近的副本
            const StorageUnit& unit = obj->getReplica(closestReplicaIndex);
            int diskId = unit.diskId;
            
            if (request.remainingUnits.find(diskId) == request.remainingUnits.end()) {
                request.remainingUnits[diskId] = std::set<int>();
            }
            
            // 添加需要读取的单元位置
            for (const auto& blockPair : unit.blockLists) {
                int startPos = blockPair.first;
                int length = blockPair.second;
                
                for (int j = 0; j < length; j++) {
                    int unitPos = startPos + j;
                    request.remainingUnits[diskId].insert(unitPos);
                    
                    // 注册块到请求的映射关系
                    registerBlockToRequest(diskId, unitPos, requestId);
                }
            }
        }
        
        // 将请求设置为处理中
        request.status = REQUEST_PROCESSING;
        processingRequests.insert(requestId);
        
        // 为磁盘磁头管理器分配读取任务
        for (auto& diskUnits : request.remainingUnits) {
            int diskId = diskUnits.first;
            std::set<int>& units = diskUnits.second;
            
            for (int unitPos : units) {
                diskHeadManager.addReadRequest(diskId, unitPos);
            }
        }
    }
    
    return true;
}

void ReadRequestManager::registerBlockToRequest(int diskId, int unitPos, int requestId) {
    blockToRequests[diskId][unitPos].insert(requestId);
}

void ReadRequestManager::unregisterBlockToRequest(int diskId, int unitPos, int requestId) {
    auto diskIt = blockToRequests.find(diskId);
    if (diskIt != blockToRequests.end()) {
        auto unitIt = diskIt->second.find(unitPos);
        if (unitIt != diskIt->second.end()) {
            unitIt->second.erase(requestId);
            
            // 如果该单元没有关联的请求了，删除它
            if (unitIt->second.empty()) {
                diskIt->second.erase(unitPos);
                
                // 如果该磁盘没有关联的单元了，删除它
                if (diskIt->second.empty()) {
                    blockToRequests.erase(diskId);
                }
            }
        }
    }
}

void ReadRequestManager::updateAllRequestsStatus(const std::unordered_map<int, std::vector<int>>& readUnits) {
    // 跟踪已更新的请求，避免重复更新
    std::unordered_set<int> updatedRequests;
    
    // 对于每个被读取的磁盘的每个单元
    for (const auto& [diskId, units] : readUnits) {
        for (int unitPos : units) {
            // 获取与该单元关联的所有请求
            auto diskIt = blockToRequests.find(diskId);
            if (diskIt != blockToRequests.end()) {
                auto unitIt = diskIt->second.find(unitPos);
                if (unitIt != diskIt->second.end()) {
                    // 更新每个关联的请求
                    for (int requestId : unitIt->second) {
                        // 避免重复更新
                        // if (updatedRequests.find(requestId) == updatedRequests.end()) {
                            auto requestIt = requests.find(requestId);
                            if (requestIt != requests.end() && requestIt->second.status == REQUEST_PROCESSING) {
                                // 更新该请求的状态
                                ReadRequest& request = requestIt->second;
                                
                                // 从剩余单元中移除已读取的单元
                                auto& diskUnits = request.remainingUnits[diskId];
                                if (diskUnits.erase(unitPos) == 1) {
                                    request.totalRemainingUnits--;
                                    
                                    // 检查请求是否已完成
                                    if (request.totalRemainingUnits == 0) {
                                        request.status = REQUEST_COMPLETED;
                                        processingRequests.erase(requestId);
                                        completedRequests.insert(requestId);
                                    }
                                }
                            }
                            
                            // 标记该请求为已更新
                            updatedRequests.insert(requestId);
                        // }
                    }
                    
                    // 清除该单元位置的所有映射，因为它已经被读取
                    unitIt->second.clear();
                    diskIt->second.erase(unitPos);
                }
            }
        }
    }
}

void ReadRequestManager::executeTimeSlice() {
    // 分配读取请求
    allocateReadRequests();
    
    // 重置磁盘磁头管理器时间片，生成读取任务
    diskHeadManager.resetTimeSlice();

    // 打印任务队列
    diskHeadManager.printTaskQueues();
    
    // 执行时间片并获取读取的单元
    std::unordered_map<int, std::vector<int>> readUnits = diskHeadManager.executeTasks();
    
    // 使用高效方法更新所有请求状态
    updateAllRequestsStatus(readUnits);
    
    // 输出当前时间片完成的请求
    if (!completedRequests.empty()) {
        std::cout << completedRequests.size() << std::endl;
        for (int requestId : completedRequests) {
            std::cout << requestId << std::endl;
        }
    }
    else {
        std::cout << 0 << std::endl;
    }

    // 清空当前时间片完成的请求记录
    resetTimeSlice();
}

std::vector<int> ReadRequestManager::getCompletedRequests() const {
    return std::vector<int>(completedRequests.begin(), completedRequests.end());
}

int ReadRequestManager::getTotalRequestCount() const {
    return requests.size();
}

int ReadRequestManager::getProcessingRequestCount() const {
    return processingRequests.size();
}

int ReadRequestManager::getCompletedRequestCount() const {
    int count = 0;
    for (const auto& entry : requests) {
        if (entry.second.status == REQUEST_COMPLETED) {
            count++;
        }
    }
    return count;
}

void ReadRequestManager::resetTimeSlice() {
    // 从各种数据结构中删除已完成的请求
    for (int requestId : completedRequests) {
        auto requestIt = requests.find(requestId);
// #ifndef NDEBUG
        if (requestIt == requests.end()) {
            continue;
        }
// #endif
        
        // 获取对象ID，以便从objectToRequests中移除引用
        int objectId = requestIt->second.objectId;
        
        // 从objectToRequests中移除请求引用
        auto objectIt = objectToRequests.find(objectId);

        if (objectIt != objectToRequests.end()) {

            objectIt->second.erase(requestId);
            
            // 如果对象没有关联的请求了，移除整个映射项
            if (objectIt->second.empty()) {
                objectToRequests.erase(objectId);
            }

        }

        
        // 清除剩余的块到请求映射
        for (const auto& [diskId, units] : requestIt->second.remainingUnits) {
            for (int unitPos : units) {
                unregisterBlockToRequest(diskId, unitPos, requestId);
            }
        }
        
        // 从请求映射中删除请求
        requests.erase(requestId);
        
        // 确保从处理中队列也删除（虽然应该在updateRequestStatus中已经处理）
        processingRequests.erase(requestId);
    }
    
    // 清空当前时间片完成的请求记录
    completedRequests.clear();
}

std::vector<int> ReadRequestManager::cancelRequestsByObjectId(int objectId) {
    std::vector<int> cancelledRequests;
    
    // 使用objectToRequests映射直接获取与对象相关的所有请求
    auto requestsIt = objectToRequests.find(objectId);
// #ifndef NDEBUG
    if (requestsIt == objectToRequests.end()) {
        // 如果对象没有关联的请求，直接返回空向量
        objectManager.deleteObject(objectId);
        return cancelledRequests;
    }
// #endif

    // 获取与该对象关联的所有请求ID
    std::unordered_set<int> requestIds = requestsIt->second;
    
    // 处理每个关联的请求
    for (int requestId : requestIds) {
        auto requestIt = requests.find(requestId);
// #ifndef NDEBUG
        if (requestIt == requests.end()) {
            continue;
        }
// #endif

        ReadRequest& request = requestIt->second;
        
        // 将请求ID添加到返回结果中
        cancelledRequests.push_back(requestId);
        
        // 从处理中队列移除
        processingRequests.erase(requestId);
        
        // 遍历请求中所有磁盘的读取单元
        for (const auto& diskUnits : request.remainingUnits) {
            int diskId = diskUnits.first;
            const std::set<int>& units = diskUnits.second;
            
            // 取消磁盘头管理器中的所有读取请求
            for (int unitPos : units) {
                diskHeadManager.cancelReadRequest(diskId, unitPos);
                
                // 清除块到请求的映射
                unregisterBlockToRequest(diskId, unitPos, requestId);
            }
        }
        
        // 从请求列表中移除此请求
        requests.erase(requestId);
    }
    
    // 从映射中删除该对象ID
    objectToRequests.erase(objectId);

    // 删除对象
    objectManager.deleteObject(objectId);
    
    return cancelledRequests;
}

int ReadRequestManager::getPendingRequestCount() const {
    return pendingRequests.size();
} 