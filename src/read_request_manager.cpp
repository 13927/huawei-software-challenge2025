#include "read_request_manager.h"
#include <iostream>
#include <random>

ReadRequestManager::ReadRequestManager(ObjectManager& objMgr, DiskHeadManager& diskMgr)
    : objectManager(objMgr), diskHeadManager(diskMgr) {

    // diskLoadCounter.resize(diskHeadManager.getDiskCount() + 1, 0); // 假设最大磁盘ID为99
}

bool ReadRequestManager::addReadRequest(int requestId, int objectId) {
    // 检查请求ID是否已存在
    if (requests.find(requestId) != requests.end()) {
        std::cout << "警告: 请求ID " << requestId << " 已存在" << std::endl;
        return false;
    }
    
    // 获取对象信息
    auto obj = objectManager.getObject(objectId);
    if (!obj) {
        std::cout << "错误: 对象ID " << objectId << " 不存在" << std::endl;
        return false;
    }
    
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
    
    // std::cout << "添加读取请求: ID=" << requestId << ", 对象ID=" << objectId 
    //           << ", 总单元数=" << request.totalRemainingUnits << std::endl;
    
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
        if (requestIt == requests.end()) {
            continue; // 跳过不存在的请求
        }
        
        ReadRequest& request = requestIt->second;
        
        // 获取对象信息
        auto obj = objectManager.getObject(request.objectId);
        if (!obj) {
            // 对象已不存在，移除此请求
            requests.erase(requestId);
            continue;
        }
        
        // 初始化请求的磁盘单元信息
        if (request.remainingUnits.empty()) {
            // 记录每个磁盘上需要读取的存储单元
            //随机选一个副本，待优化

            std::default_random_engine engine(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 2);
            int random_number = dist(engine);
            
            const StorageUnit& unit = obj->getReplica(random_number);
            int diskId = unit.diskId;
            
            if (request.remainingUnits.find(diskId) == request.remainingUnits.end()) {
                request.remainingUnits[diskId] = std::set<int>();
            }
            // TODO: 
            // 添加需要读取的单元位置
            for (const auto& blockPair : unit.blockLists) {
                int startPos = blockPair.first;
                int length = blockPair.second;
                
                for (int j = 0; j < length; j++) {
                    request.remainingUnits[diskId].insert(startPos + j);
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

void ReadRequestManager::updateRequestStatus(int requestId, const std::unordered_map<int, std::vector<int>>& readUnits) {
    // 检查请求是否存在
    auto it = requests.find(requestId);
    if (it == requests.end() || it->second.status == REQUEST_COMPLETED) {
        return;
    }
    
    ReadRequest& request = it->second;
    
    // 更新每个磁盘上已读取的单元
    for (const auto& entry : readUnits) {
        int diskId = entry.first;
        const std::vector<int>& units = entry.second;
        
        // 跳过该请求没有关联的磁盘
        if (request.remainingUnits.find(diskId) == request.remainingUnits.end()) {
            continue;
        }
        
        // 从剩余单元中移除已读取的单元
        for (int unit : units) {
            auto unitIt = request.remainingUnits[diskId].find(unit);
            if (unitIt != request.remainingUnits[diskId].end()) {
                request.remainingUnits[diskId].erase(unitIt);
                request.totalRemainingUnits--;
            }
        }
    }
    
    // 检查请求是否已完成
    if (request.totalRemainingUnits == 0) {
        request.status = REQUEST_COMPLETED;
        processingRequests.erase(requestId);
        completedRequests.insert(requestId);
        // std::cout << "请求完成: ID=" << requestId << ", 对象ID=" << request.objectId << std::endl;
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
    
    // 更新所有正在处理的请求状态
    std::vector<int> processingRequestsCopy(processingRequests.begin(), processingRequests.end());
    for (int requestId : processingRequestsCopy) {
        updateRequestStatus(requestId, readUnits);
    }
    
    // 输出当前时间片完成的请求
    if (!completedRequests.empty()) {
        // std::cout << "当前时间片完成的请求: ";
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
        if (requestIt != requests.end()) {
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
        }
        
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
    if (requestsIt != objectToRequests.end()) {
        // 获取与该对象关联的所有请求ID
        std::unordered_set<int> requestIds = requestsIt->second;
        
        // 处理每个关联的请求
        for (int requestId : requestIds) {
            auto requestIt = requests.find(requestId);
            if (requestIt != requests.end()) {
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
        }
        
        // 从映射中删除该对象ID
        objectToRequests.erase(objectId);
    }

    // 删除对象
    objectManager.deleteObject(objectId);
        

    
    return cancelledRequests;
}

int ReadRequestManager::getPendingRequestCount() const {
    return pendingRequests.size();
} 