#ifndef READ_REQUEST_MANAGER_H
#define READ_REQUEST_MANAGER_H

// TODO: 
// 1. allocateReadRequests()
// 2. 处理删除

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <set>
#include "object_manager.h"
#include "disk_head_manager.h"
#include "constants.h"

// 读取请求状态
enum RequestStatus {
    REQUEST_PENDING,     // 等待处理
    REQUEST_PROCESSING,  // 处理中
    REQUEST_COMPLETED    // 已完成
};

// 读取请求数据结构
struct ReadRequest {
    int requestId;                                  // 请求ID
    int objectId;                                   // 读取的对象ID
    RequestStatus status;                           // 请求状态
    std::unordered_map<int, std::set<int>> remainingUnits; // 每个磁盘上需要读取的单元 (磁盘ID -> 单元位置集合)
    int totalRemainingUnits;                        // 总剩余需要读取的单元数
    
    ReadRequest() : requestId(0), objectId(0), status(REQUEST_PENDING), totalRemainingUnits(0) {}
    ReadRequest(int reqId, int objId) : requestId(reqId), objectId(objId), status(REQUEST_PENDING), totalRemainingUnits(0) {}
};

// 读取请求管理器类
class ReadRequestManager {
private:
    ObjectManager& objectManager;                        // 对象管理器引用
    DiskHeadManager& diskHeadManager;                    // 磁盘头管理器引用
    
    std::unordered_map<int, ReadRequest> requests;       // 所有请求 (请求ID -> 请求)
    std::vector<int> pendingRequests;                    // 等待处理的请求ID列表
    std::unordered_set<int> processingRequests;          // 正在处理的请求ID集合
    std::unordered_set<int> completedRequests;           // 当前时间片完成的请求ID集合
    
    // 对象ID到请求ID的映射，用于快速找到与对象相关的读取请求
    std::unordered_map<int, std::unordered_set<int>> objectToRequests;

public:
    ReadRequestManager(ObjectManager& objMgr, DiskHeadManager& diskMgr);
    
    // 添加读取请求
    bool addReadRequest(int requestId, int objectId);
    
    // 分配读取请求（将等待的请求变为处理中）
    bool allocateReadRequests();
    
    // 更新请求状态
    void updateAllRequestsStatus(const std::unordered_map<int, std::vector<int>>& readUnits);
    
    // 取消某个对象的所有读取请求
    std::vector<int> cancelRequestsByObjectId(int objectId);
    
    // 执行一个时间片
    void executeTimeSlice();
    
    // 重置时间片数据
    void resetTimeSlice();
    
    // 获取完成的请求列表
    std::vector<int> getCompletedRequests() const;
    
    // 获取请求数量信息
    int getTotalRequestCount() const;
    int getProcessingRequestCount() const;
    int getCompletedRequestCount() const;
    int getPendingRequestCount() const;
    
};

#endif // READ_REQUEST_MANAGER_H 