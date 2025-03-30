#ifndef DISK_HEAD_MANAGER_H
#define DISK_HEAD_MANAGER_H

#include <vector>
#include <queue>
#include <unordered_map>
#include <set>
#include <string>
#include "disk_manager.h"

// 磁头动作类型
enum HeadActionType {
    ACTION_JUMP = 0,  // 跳跃到指定位置，消耗G个令牌
    ACTION_PASS = 1,  // 跳过当前单元，消耗1个令牌
    ACTION_READ = 2   // 读取当前单元，消耗令牌数根据规则计算
};

// 磁头任务结构体
struct HeadTask {
    HeadActionType actionType;  // 动作类型
    int targetUnit;             // 目标存储单元（Jump动作需要）
    
    HeadTask(HeadActionType type, int target = 0) 
        : actionType(type), targetUnit(target){}
};

// 磁头状态结构体
struct HeadState {
    int currentPosition;        // 当前位置
    HeadActionType lastAction;  // 上一次动作类型
    int lastTokenCost;          // 上一次动作消耗的令牌数
    
    HeadState() : currentPosition(1), lastAction(ACTION_PASS), 
                 lastTokenCost(0){}
};

// 磁盘磁头管理器类
class DiskHeadManager {
private:
    int diskCount;              // 磁盘数量
    int unitCount;              // 每个磁盘的存储单元数
    int maxTokensPerSlice;      // 每个时间片最大令牌消耗数G
    
    std::vector<HeadState> headStates;                    // 每个磁盘磁头的状态
    std::vector<std::queue<HeadTask>> taskQueues;         // 每个磁盘磁头的任务队列
    
    // 存储每个磁盘上需要读取的存储单元
    std::vector<std::set<int>> diskReadUnits;
    
    // DiskManager引用
    DiskManager& diskManager;
    
    // 计算Read动作的令牌消耗
    int calculateReadTokenCost(int diskId);
    
    // 为特定磁盘生成下一批任务
    void generateTasksForDisk(int diskId);
    
    // 找到下一个需要读取的单元
    int findNextReadUnit(int diskId, int currentPos);
    
    // 计算到目标单元需要的Pass次数
    int calculatePassCount(int from, int to);

    // 检查指定磁盘上指定位置是否存在高读取密度
    bool isReadDensityHigh(int diskId, int currentPos, int distance);
    
public:
    DiskHeadManager(int disks, int units, int maxTokens, DiskManager& dm);

    // 获取磁盘数量
    int getDiskCount() const { return diskCount; };
    
    // 重置时间片，恢复每个磁盘的令牌数
    void resetTimeSlice();
    
    // 添加存储单元读取请求
    bool addReadRequest(int diskId, int unitPosition);
    
    // 批量添加存储单元读取请求
    bool addReadRequests(int diskId, const std::vector<int>& unitPositions);
    
    // 取消存储单元读取请求
    bool cancelReadRequest(int diskId, int unitPosition);

    // 批量取消存储单元读取请求
    bool cancelReadRequests(int diskId, const std::vector<int>& unitPositions);
    
    // 取消磁盘上所有读取请求
    void cancelAllReadRequests(int diskId);
    
    // 为当前时间片生成任务队列
    void generateTasks();
    
    // 获取磁头当前位置
    int getHeadPosition(int diskId) const;
    
    // 清空指定磁盘的任务队列
    void clearTaskQueue(int diskId);
    
    // 获取任务队列长度
    int getTaskQueueSize(int diskId) const;
    
    // 检查磁盘上是否有待读取的单元
    bool hasReadRequests(int diskId) const;
    
    // 获取磁盘上待读取单元数
    int getReadRequestCount(int diskId) const;
    
    // 检查特定单元是否需要读取
    bool needsRead(int diskId, int unitPosition) const;
    
    // 按照规则输出任务队列
    void printTaskQueues() const;
    
    // 获取指定磁盘的任务队列字符串表示
    std::string getTaskQueueString(int diskId) const;

    // 执行任务并返回本时间片读取的存储单元 <diskId, [读取的存储单元]>
    std::unordered_map<int, std::vector<int>> executeTasks();

    // 获取磁盘单元数
    int getUnitCount() const { return unitCount; }
    
    // 获取DiskManager引用
    const DiskManager& getDiskManager() const { return diskManager; }

    // 获取磁头未读取的单元数
    int getHeadReadLoad(int diskId) const { return diskReadUnits[diskId].size(); }

    // 检查指定存储单元周围存在要读取的单元数量
    int checkSurroundingReadUnits(int diskId, int unitPos, int length, int checkRange) const;

    // 获取最近的读取单元距离
    int getDistanceOfNearestReadUnit(int diskId, int startPos, int length) const;
};

#endif // DISK_HEAD_MANAGER_H 