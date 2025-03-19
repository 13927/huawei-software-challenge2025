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
enum ReadRequestStatus {
    REQUEST_PENDING,    // 等待处理
    REQUEST_PROCESSING, // 正在处理
    REQUEST_COMPLETED   // 已完成
};

// 读取请求结构体
struct ReadRequest {
    int requestId;                          // 请求ID
    int objectId;                           // 对象ID
    ReadRequestStatus status;               // 请求状态
    std::unordered_map<int, std::set<int>> remainingUnits;  // 每个磁盘上剩余未读取的单元 <diskId, set<unitPosition>>
    int totalRemainingUnits;                // 剩余未读取的单元总数
    
    // 默认构造函数
    ReadRequest() 
        : requestId(0), objectId(0), status(REQUEST_PENDING), totalRemainingUnits(0) {}
    
    ReadRequest(int reqId, int objId) 
        : requestId(reqId), objectId(objId), status(REQUEST_PROCESSING), totalRemainingUnits(0) {}
};


class ReadRequestManager {
private:
    ObjectManager& objectManager;          // 对象管理器引用
    DiskHeadManager& diskHeadManager;      // 磁盘磁头管理器引用
    
    std::unordered_map<int, ReadRequest> requests;  // 所有读取请求 <requestId, ReadRequest>
    std::unordered_map<int, std::unordered_set<int>> objectToRequests;
    std::vector<int> pendingRequests;  // 等待处理的请求 <requestId, ReadRequest>
    std::unordered_set<int> processingRequests;     // 正在处理的请求ID
    std::set<int> completedRequests;                // 已完成的请求ID（当前时间片）
    
    // 更新请求状态
    void updateRequestStatus(int requestId, const std::unordered_map<int, std::vector<int>>& readUnits);
    
    // 获取负载最小的磁盘
    int getLeastLoadedDisk(const std::vector<int>& availableDisks);
    
public:
    ReadRequestManager(ObjectManager& objMgr, DiskHeadManager& diskMgr);
    
    // 添加读取请求
    bool addReadRequest(int requestId, int objectId);

    bool allocateReadRequests();
    
    // 执行当前时间片的任务
    void executeTimeSlice();
    
    // 获取当前时间片完成的读取请求
    std::vector<int> getCompletedRequests() const;
    
    // 获取请求总数
    int getTotalRequestCount() const;
    
    // 获取等待处理的请求数
    int getPendingRequestCount() const;
    
    // 获取正在处理的请求数
    int getProcessingRequestCount() const;
    
    // 获取已完成的请求总数（所有时间片）
    int getCompletedRequestCount() const;
    
    // 重置时间片，准备执行下一个时间片
    void resetTimeSlice();
    
    // 根据对象ID取消所有相关的读取请求，并删除对象
    // 返回被取消的请求ID列表
    std::vector<int> cancelRequestsByObjectId(int objectId);
};

#endif // READ_REQUEST_MANAGER_H 