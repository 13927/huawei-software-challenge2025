#include "read_request_manager.h"
#include <climits>
#include <iostream>
#include "constants.h" 
#include <algorithm>
#include <fstream>

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
            // 查找是否有处理中的请求正在读取相同的对象
            bool hasProcessingRequest = false;

            // 请求读取对象的待读取的索引，初始化为0到object.size()
            std::unordered_set<int> unprocessedBlockIndex;
            for (int i = 0; i < obj->getSize(); i++) {
                unprocessedBlockIndex.insert(i);
            }
            // 待处理的磁盘单元索引 <磁盘id, 单元索引>
            std::vector<std::pair<int, int>> processedDiskUnit;

            auto objectRequestsIt = objectToRequests.find(request.objectId);
            if (objectRequestsIt != objectToRequests.end()) {
                // 检查与该对象关联的所有请求
                for (int otherRequestId : objectRequestsIt->second) {
                    // 跳过当前请求自身
                    if (otherRequestId == requestId) {
                        continue;
                    }
                    
                    auto otherRequestIt = requests.find(otherRequestId);
                    if (otherRequestIt != requests.end() && otherRequestIt->second.status == REQUEST_PROCESSING) {
                        // 找到一个处理中的相同对象请求
                        hasProcessingRequest = true;
                        const ReadRequest& otherRequest = otherRequestIt->second;
                        
                        // 对于每个磁盘，记录正在处理的单元
                        for (const auto& [diskId, units] : otherRequest.remainingUnits) {
                            for (int unitPos : units) {
                                int blockIndex = diskHeadManager.getDiskManager().getBlockStatus(diskId, unitPos);
                                if (unprocessedBlockIndex.erase(blockIndex) > 0) {
                                    processedDiskUnit.push_back(std::make_pair(diskId, unitPos));
                                }
                            }
                        }
                    }
                }
            }
            
            if (hasProcessingRequest && unprocessedBlockIndex.empty()) {

                for (auto [diskId, unitPos] : processedDiskUnit) {
                    request.remainingUnits[diskId].insert(unitPos);
                }
    
            } else {
                // 没有处理中的相同对象请求，使用新的逻辑选择最优副本
                
                // 存储每个副本的评估信息 <副本索引, <距离评分, 磁盘ID, 磁盘负载>>
                std::vector<std::tuple<int, int, int, int>> replicaScores;
                
                // 检查每个副本
                for (int replicaIndex = 0; replicaIndex < REP_NUM; replicaIndex++) {
                    const StorageUnit& replica = obj->getReplica(replicaIndex);
                    int diskId = replica.diskId;
                    
                    // 获取该磁盘的未读取单元数
                    double diskLoad = diskHeadManager.getHeadReadLoad(diskId);
                    
                    // 计算副本到最近读取单元的距离
                    int totalDistance = INT_MAX;
                    for (const auto& blockPair : replica.blockLists) {
                        int startPos = blockPair.first;
                        int length = blockPair.second;
                        
                        // 使用DiskHeadManager的方法获取到最近读取单元的距离
                        int distance = diskHeadManager.getDistanceOfNearestReadUnit(diskId, startPos, length);
                        totalDistance = std::min(totalDistance, distance);
                    }
                    
                    // 保存副本的评分信息
                    replicaScores.push_back(std::make_tuple(replicaIndex, totalDistance, diskId, diskLoad));
                }
                
                // 找出负载最小和最大的磁盘
                int minLoad = INT_MAX;
                int maxLoad = 0;
                
                for (const auto& [replicaIndex, distance, diskId, load] : replicaScores) {
                    minLoad = std::min(minLoad, load);
                    maxLoad = std::max(maxLoad, load);
                }
                
                // 计算负载差距
                double loadDifference = (double)(maxLoad - minLoad) / (double)(maxLoad);
                
                // 选择要使用的副本
                int selectedReplicaIndex = -1;
                
                if (loadDifference > 0.65) {
                    // 负载差距大于60%，选择负载最小的磁盘
                    int minLoadIndex = -1;
                    int currentMinLoad = INT_MAX;
                    
                    for (size_t i = 0; i < replicaScores.size(); i++) {
                        const auto& [replicaIndex, distance, diskId, load] = replicaScores[i];
                        if (load < currentMinLoad) {
                            currentMinLoad = load;
                            minLoadIndex = i;
                        }
                    }
                    
                    if (minLoadIndex != -1) {
                        selectedReplicaIndex = std::get<0>(replicaScores[minLoadIndex]);
                    }
                } else {
                    // 负载差距不大，选择距离最近的副本
                    int minDistanceIndex = -1;
                    int currentMinDistance = INT_MAX;
                    
                    for (size_t i = 0; i < replicaScores.size(); i++) {
                        const auto& [replicaIndex, distance, diskId, load] = replicaScores[i];
                        if (distance < currentMinDistance) {
                            currentMinDistance = distance;
                            minDistanceIndex = i;
                        }
                    }
                    
                    if (minDistanceIndex != -1) {
                        selectedReplicaIndex = std::get<0>(replicaScores[minDistanceIndex]);
                    }
                }
                
                // 如果找不到有效副本，使用第一个副本（不应该发生）
                if (selectedReplicaIndex == -1 && !replicaScores.empty()) {
                    selectedReplicaIndex = std::get<0>(replicaScores[0]);
                }
                
                // 使用选定的副本
                const StorageUnit& unit = obj->getReplica(selectedReplicaIndex);
                int diskId = unit.diskId;
                
                if (hasProcessingRequest) {
                    for (auto [diskId, unitPos] : processedDiskUnit) {
                        request.remainingUnits[diskId].insert(unitPos);
                    }

                    for (const auto& blockPair : unit.blockLists) {
                        int startPos = blockPair.first;
                        int length = blockPair.second;
                        
                        for (int j = 0; j < length; j++) {
                            int unitPos = startPos + j;
                            int blockIndex = diskHeadManager.getDiskManager().getBlockStatus(diskId, unitPos);
                            auto it = unprocessedBlockIndex.find(blockIndex);
                            if (it != unprocessedBlockIndex.end()) {
                                request.remainingUnits[diskId].insert(unitPos);
                                unprocessedBlockIndex.erase(it);
                            }
                        }
                    }

                }
                else {
                    // 添加需要读取的单元位置
                    for (const auto& blockPair : unit.blockLists) {
                        int startPos = blockPair.first;
                        int length = blockPair.second;
                        
                        for (int j = 0; j < length; j++) {
                            int unitPos = startPos + j;
                            request.remainingUnits[diskId].insert(unitPos);
                        }
                    }
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

void ReadRequestManager::updateAllRequestsStatus(const std::unordered_map<int, std::vector<int>>& readUnits) {
    
    // 对于每个被读取的磁盘的每个单元
    for (const auto& [diskId, units] : readUnits) {
        for (int unitPos : units) {
            // 使用ObjectManager获取该单元对应的对象ID
            int objectId = objectManager.getObjectIdByDiskBlock(diskId, unitPos);
            if (objectId == -1) {
                continue; // 没有找到对应的对象，跳过
            }
            
            // 获取与该对象关联的所有请求
            auto objectIt = objectToRequests.find(objectId);
            if (objectIt != objectToRequests.end()) {
                // 更新每个关联的请求
                for (int requestId : objectIt->second) {
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
                }
            }
        }
    }
}

void ReadRequestManager::executeTimeSlice() {
    // 分配读取请求
    allocateReadRequests();

    #ifndef NDEBUG
    // 将当前时间片每个磁盘磁头的待读取单元数量写入txt
    std::ofstream file("disk_head_load.txt", std::ios::app);
    file << "TIMESTAMP " << currentTimeSlice << std::endl;
    for (int i = 1; i <= diskHeadManager.getDiskCount(); i++) {
        file << diskHeadManager.getHeadReadLoad(i) << std::endl;
    }
    file.close();
    #endif
    
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