#include "disk_head_manager.h"
#include <algorithm>
#include <iostream>
#include <sstream>

DiskHeadManager::DiskHeadManager(int disks, int units, int maxTokens) 
    : diskCount(disks), unitCount(units), maxTokensPerSlice(maxTokens) {
    // 初始化每个磁盘的磁头状态和任务队列
    headStates.resize(disks + 1);  // 索引从1开始
    taskQueues.resize(disks + 1);
    diskReadUnits.resize(disks + 1);
}

void DiskHeadManager::resetTimeSlice() {
    for (int diskId = 1; diskId <= diskCount; diskId++) {
        headStates[diskId].actionInCurrentSlice = false;
    }
    
    // 每个时间片开始时，重新为各个磁盘生成任务
    generateTasks();
}

bool DiskHeadManager::addReadRequest(int diskId, int unitPosition) {
#ifndef NDEBUG
    // 检查参数的有效性
    if (diskId < 1 || diskId > diskCount || 
        unitPosition < 1 || unitPosition > unitCount) {
        return false;
    }
#endif
    
    // 添加读取请求
    diskReadUnits[diskId].insert(unitPosition);
    return true;
}

bool DiskHeadManager::addReadRequests(int diskId, const std::vector<int>& unitPositions) {
#ifndef NDEBUG
    // 检查磁盘ID的有效性
    if (diskId < 1 || diskId > diskCount) {
        return false;
    }
    
    // 添加所有读取请求
    bool success = true;
    for (int pos : unitPositions) {
        if (pos < 1 || pos > unitCount) {
            success = false;
        } else {
            diskReadUnits[diskId].insert(pos);
        }
    }
    return success;
#else
    // 直接添加所有读取请求
    for (int pos : unitPositions) {
        diskReadUnits[diskId].insert(pos);
    }
    return true;
#endif
}

bool DiskHeadManager::cancelReadRequest(int diskId, int unitPosition) {
#ifndef NDEBUG
    // 检查参数的有效性
    if (diskId < 1 || diskId > diskCount || 
        unitPosition < 1 || unitPosition > unitCount) {
        return false;
    }
#endif
    
    // 如果存在该读取请求，则移除
    auto& readUnits = diskReadUnits[diskId];
    auto it = readUnits.find(unitPosition);
    if (it != readUnits.end()) {
        readUnits.erase(it);
        return true;
    }
    return false;
}

bool DiskHeadManager::cancelReadRequests(int diskId, const std::vector<int>& unitPositions) {
#ifndef NDEBUG
    // 检查磁盘ID的有效性
    if (diskId < 1 || diskId > diskCount) {
        return false;
    }

    // 检查每个单元位置的有效性
    for (int pos : unitPositions) {
        if (pos < 1 || pos > unitCount) {
            return false;
        }
    }
#endif

    // 移除所有读取请求
    for (int pos : unitPositions) {
        diskReadUnits[diskId].erase(pos);
    }

    return true;
}

void DiskHeadManager::cancelAllReadRequests(int diskId) {
#ifndef NDEBUG
    // 检查磁盘ID的有效性
    if (diskId < 1 || diskId > diskCount) {
        return;
    }
#endif
    
    // 清空该磁盘的所有读取请求
    diskReadUnits[diskId].clear();
    
    // 清空任务队列
    clearTaskQueue(diskId);
}

void DiskHeadManager::generateTasks() {
    // 为每个磁盘生成任务
    for (int diskId = 1; diskId <= diskCount; diskId++) {
        generateTasksForDisk(diskId);
    }
}

void DiskHeadManager::generateTasksForDisk(int diskId) {
    // 先清空当前任务队列
    clearTaskQueue(diskId);
    
    // 如果没有待读取的单元，不需要生成任务
    if (diskReadUnits[diskId].empty()) {
        return;
    }
    
    // 获取当前磁头位置和可用令牌数
    int currentPos = headStates[diskId].currentPosition;
    int availableTokens = maxTokensPerSlice;
    
    // 循环生成任务，直到没有更多需要读取的单元或无法生成有效任务
    while (!diskReadUnits[diskId].empty() && availableTokens > 0) {
        // 找到下一个需要读取的单元
        int nextUnit = findNextReadUnit(diskId, currentPos);
        
        // 如果找不到需要读取的单元，退出循环
        if (nextUnit == -1) {
            break;
        }
        
        // 若当前位置就是需要读取的单元
        if (nextUnit == currentPos) {
            // 计算READ任务的令牌消耗
            int readCost = calculateReadTokenCost(diskId);
            
            // 如果令牌不足，无法继续生成任务
            if (readCost > availableTokens) {
                break;
            }
            
            // 添加读取任务
            HeadTask readTask(ACTION_READ, nextUnit);
            taskQueues[diskId].push(readTask);
            
            // 更新可用令牌和当前位置
            availableTokens -= readCost;
            
            // 更新虚拟当前位置（为了生成后续任务）
            currentPos = (currentPos % unitCount) + 1;
            
            // 更新磁头状态
            headStates[diskId].lastAction = ACTION_READ;
            headStates[diskId].lastTokenCost = readCost;    

            diskReadUnits[diskId].erase(nextUnit);       
            continue;
        }
        
        // 计算从当前位置到下一个需要读取的位置所需的PASS次数
        int passCount = calculatePassCount(currentPos, nextUnit);
        
        if (availableTokens == maxTokensPerSlice && passCount + 64 > availableTokens) {
            // 使用JUMP操作（只能在时间片开始时执行）
            HeadTask jumpTask(ACTION_JUMP, nextUnit);
            taskQueues[diskId].push(jumpTask);

            currentPos = nextUnit;
            headStates[diskId].lastAction = ACTION_JUMP;
            headStates[diskId].lastTokenCost = maxTokensPerSlice;  
            // JUMP执行后，该时间片不能再执行其他操作
            break;
        } else {
            // 使用PASS移动
            
            // 尽可能多地执行PASS，但不超过到目标单元所需的PASS数
            int executedPass = std::min(availableTokens, passCount);
            
            // 添加PASS任务
            for (int i = 0; i < executedPass; i++) {
                HeadTask passTask(ACTION_PASS);
                taskQueues[diskId].push(passTask);
            }
            
            // 更新可用令牌和虚拟当前位置
            availableTokens -= executedPass;
            currentPos = (currentPos + executedPass)% unitCount;
            headStates[diskId].lastAction = ACTION_PASS;
            headStates[diskId].lastTokenCost = 1;  
        }
    }
    
    headStates[diskId].currentPosition = currentPos;
}

int DiskHeadManager::findNextReadUnit(int diskId, int currentPos) {
    // 如果没有待读取的单元，返回-1
    if (diskReadUnits[diskId].empty()) {
        return -1;
    }
    
    // 使用 lower_bound 寻找大于等于当前位置的第一个单元
    auto it = diskReadUnits[diskId].lower_bound(currentPos);
    
    // 如果找到大于等于当前位置的单元
    if (it != diskReadUnits[diskId].end()) {
        return *it;
    }
    
    // 如果没有找到大于等于当前位置的单元，返回集合中的第一个单元
    return *diskReadUnits[diskId].begin();
}

int DiskHeadManager::calculatePassCount(int from, int to) {
    // 如果目标位置在当前位置之后，直接计算差值
    if (to > from) {
        return to - from;
    }
    
    // 如果目标位置在当前位置之前或等于，需要经过环形路径
    return unitCount - from + to;
}

int DiskHeadManager::calculateReadTokenCost(int diskId) {
    const HeadState& state = headStates[diskId];
    
    // 如果上一次动作不是Read，则消耗64个令牌
    if (state.lastAction != ACTION_READ) {
        return 64;
    }
    
    // 根据规则计算令牌消耗
    return std::max(16, (int)std::ceil(state.lastTokenCost * 0.8));
}

int DiskHeadManager::getHeadPosition(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount) {
        return -1;
    }
#endif
    return headStates[diskId].currentPosition;
}

void DiskHeadManager::clearTaskQueue(int diskId) {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount) {
        return;
    }
#endif
    
    while (!taskQueues[diskId].empty()) {
        taskQueues[diskId].pop();
    }
}

int DiskHeadManager::getTaskQueueSize(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount) {
        return 0;
    }
#endif
    return taskQueues[diskId].size();
}

bool DiskHeadManager::hasReadRequests(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount) {
        return false;
    }
#endif
    return !diskReadUnits[diskId].empty();
}

int DiskHeadManager::getReadRequestCount(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount) {
        return 0;
    }
#endif
    return diskReadUnits[diskId].size();
}

bool DiskHeadManager::needsRead(int diskId, int unitPosition) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount || unitPosition < 1 || unitPosition > unitCount) {
        return false;
    }
#endif
    
    const auto& readUnits = diskReadUnits[diskId];
    return readUnits.find(unitPosition) != readUnits.end();
}

void DiskHeadManager::printTaskQueues() const {
    for (int diskId = 1; diskId <= diskCount; diskId++) {
        std::cout << getTaskQueueString(diskId) << std::endl;
    }
}

std::string DiskHeadManager::getTaskQueueString(int diskId) const {
#ifndef NDEBUG
    if (diskId < 1 || diskId > diskCount) {
        return "";
    }
#endif
    
    std::ostringstream output;

    std::queue<HeadTask> taskQueue = taskQueues[diskId];
    
    // 检查队列是否为空
    if (taskQueue.empty()) {
        output << "#";
        return output.str();
    }
    
    // 检查第一个任务是否为JUMP
    const HeadTask& firstTask = taskQueue.front();
    if (firstTask.actionType == ACTION_JUMP) {
        output << "j " << firstTask.targetUnit;
        taskQueue.pop();
        return output.str();
    }
    
    // 输出所有非JUMP任务
    while (!taskQueue.empty()) {
        const HeadTask& task = taskQueue.front();
        
        switch (task.actionType) {
            case ACTION_PASS:
                output << "p";
                break;
            case ACTION_READ:
                output << "r";
                break;
            default:
                // 不应该到达这里，因为前面已经处理了JUMP
                break;
        }
        
        taskQueue.pop();
    }
    
    // 添加结束标记
    output << "#";
    
    return output.str();
}

// 执行任务并返回本时间片读取的存储单元
std::unordered_map<int, std::vector<int>> DiskHeadManager::executeTasks() {
    std::unordered_map<int, std::vector<int>> readUnitsInThisSlice;

    for (int diskId = 1; diskId <= diskCount; ++diskId) {
        while (!taskQueues[diskId].empty()) {
            HeadTask& task = taskQueues[diskId].front();

            if (task.actionType == ACTION_READ) {
                readUnitsInThisSlice[diskId].push_back(task.targetUnit);
                // 移除读取请求
                diskReadUnits[diskId].erase(task.targetUnit);
            }
            taskQueues[diskId].pop();
        }
    }
    
    return readUnitsInThisSlice;
}