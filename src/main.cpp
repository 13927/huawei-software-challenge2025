#include <cassert>
#include <cstdlib>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <string>
#include <utility>
#include <iomanip>
#include "disk_manager.h"
#include "object_manager.h"
#include "disk_head_manager.h"
#include "read_request_manager.h"
#include "constants.h"
#include "frequency_data.h"

// 全局变量
int T, M, N, V, G; // 总时间片数、标签数、磁盘数、每个磁盘的存储单元数、每个磁头每个时间片最多消耗的令牌数
FrequencyData freqData; // 频率数据
int currentTimeSlice = 0;

// 读取系统参数
void readSystemParameters() {
    // 读取基本参数
    std::cin >> T >> M >> N >> V >> G;
    
    // 计算分片数量
    int sliceCount = (T - 1) / FRE_PER_SLICING + 1;
    
    // 设置系统参数
    freqData.setSystemParameters(T, N, V, G);
    
    // 初始化频率数据结构
    freqData.initialize(M, sliceCount);
    
    // 读取删除操作频率
    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= sliceCount; j++) {
            std::cin >> freqData.getDeleteFrequency()[i][j];
        }
    }
    
    // 读取写入操作频率
    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= sliceCount; j++) {
            std::cin >> freqData.getWriteFrequency()[i][j];
        }
    }
    
    // 读取读取操作频率
    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= sliceCount; j++) {
            std::cin >> freqData.getReadFrequency()[i][j];
        }
    }

    std::cout << "OK" << std::endl;
    std::cout.flush();
}

// 全局预处理函数
void globalPreprocessing() {
    // 读取系统参数
    readSystemParameters();
    // 分析数据并预分配空间
    freqData.analyzeAndPreallocate();

    #ifndef NDEBUG
    // 输出磁盘分配结果
    std::ofstream outFile("allocation_result.txt");
    if (outFile.is_open()) {
        outFile << "=== 磁盘预分配结果 ===\n\n";
         
        // 输出所有标签的分配情况
        outFile << "标签分配情况:\n";
        outFile << "标签ID\t分配磁盘数\t总分配单元数\t分配详情\n";
        for (int tag = 0; tag <= M; tag++) {
            int diskCount = freqData.getTagDiskCount(tag);
            int totalUnits = freqData.getTagTotalAllocatedUnits(tag);
            auto allocation = freqData.getTagAllocation(tag);
            
            outFile << tag << "\t" << diskCount << "\t" << totalUnits << "\t";
            
            if (!allocation.empty()) {
                outFile << "分配到: ";
                for (size_t i = 0; i < allocation.size(); i++) {
                    auto [diskId, startUnit, endUnit] = allocation[i];
                    outFile << "磁盘" << diskId << "[" << startUnit << "-" << endUnit << "]";
                    if (i < allocation.size() - 1) {
                        outFile << ", ";
                    }
                }
            } else {
                outFile << "无分配";
            }
            outFile << "\n";
        }
        outFile << "\n";
        
        // 输出所有磁盘的分配情况
        outFile << "磁盘分配情况:\n";
        outFile << "磁盘ID\t分配区间数\t区间详情\n";
        for (int disk = 1; disk <= N; disk++) {
            auto diskAlloc = freqData.getDiskAllocation(disk);
            
            outFile << disk << "\t" << diskAlloc.size() << "\t";
            
            if (!diskAlloc.empty()) {
                for (size_t i = 0; i < diskAlloc.size(); i++) {
                    auto [startUnit, endUnit, tag] = diskAlloc[i];
                    outFile << "[" << startUnit << "-" << endUnit << "]:标签" << tag;
                    if (i < diskAlloc.size() - 1) {
                        outFile << ", ";
                    }
                }
            } else {
                outFile << "无分配";
            }
            outFile << "\n";
        }
        
        // 输出分配统计信息
        outFile << "\n=== 分配统计 ===\n";
        int totalAllocatedUnits = 0;
        for (int tag = 0; tag <= M; tag++) {
            totalAllocatedUnits += freqData.getTagTotalAllocatedUnits(tag);
        }
        int totalCapacity = N * V;
        double usagePercent = (double)totalAllocatedUnits / totalCapacity * 100;
        
        outFile << "总容量: " << totalCapacity << " 单元\n";
        outFile << "已分配: " << totalAllocatedUnits << " 单元\n";
        outFile << "使用率: " << std::fixed << std::setprecision(2) << usagePercent << "%\n";
        
        outFile.close();
        

    }
    
    #endif
}

void timestamp_action()
{
    int timestamp;
    std::string dummy;
    std::cin >> dummy >> timestamp;
    currentTimeSlice = timestamp;
    std::cout << "TIMESTAMP " << timestamp << std::endl;
    std::cout.flush();
}

// 处理写入事件
void handle_write_events(ObjectManager& objectManager) {
    int n_write;
    std::cin >> n_write;
    
    // 如果当前时间片没有写入事件，直接返回
    if (n_write == 0) {
        std::cout.flush();
        return;
    }
    
    // 处理每个写入事件
    for (int i = 0; i < n_write; i++) {
        int obj_id, obj_size, obj_tag;
        std::cin >> obj_id >> obj_size >> obj_tag;
        
        // 使用 ObjectManager 创建对象
        bool success = objectManager.createObject(obj_id, obj_size, obj_tag);
        
        if (success) {
            // 获取创建的对象
            auto obj = objectManager.getObject(obj_id);
            
            if (obj) {
                // 输出对象ID
                std::cout << obj_id << std::endl;
                
                // 输出三个副本的存储位置信息
                for (int rep = 0; rep < REP_NUM; rep++) {
                    const StorageUnit& replica = obj->getReplica(rep);
                    
                    // 输出副本所在的磁盘ID
                    std::cout << replica.diskId;
                    
                    // 计算总存储单元数
                    int totalUnits = 0;
                    for (const auto& blockList : replica.blockLists) {
                        totalUnits += blockList.second;
                    }
                    
                    // 输出每个存储单元的编号
                    int currentUnit = 0;
                    for (const auto& blockList : replica.blockLists) {
                        int start = blockList.first;
                        int length = blockList.second;
                        
                        // 输出此块中的每个单元
                        for (int j = 0; j < length; j++) {
                            std::cout << " " << start + j;
                            currentUnit++;
                        }
                    }
                    
                    std::cout << std::endl;
                }
            }
        }
    }
    
    // 刷新输出缓冲区
    std::cout.flush();
}

// 处理删除事件
void handle_delete_events(ReadRequestManager& requestManager) {
    int n_delete;
    std::cin >> n_delete;
    
    // 收集所有被取消的请求ID
    std::vector<int> abortedRequests;
    
    // 处理每个要删除的对象
    for (int i = 0; i < n_delete; i++) {
        int obj_id;
        std::cin >> obj_id;
        
        // 调用ReadRequestManager取消与对象相关的所有请求
        std::vector<int> cancelledReqs = requestManager.cancelRequestsByObjectId(obj_id);
        // 打印取消的请求ID到文件
        #ifndef NDEBUG
        std::ofstream logFile("cancelledReqs.txt", std::ios::app);
        if (logFile.is_open()) {
            for (int req_id : cancelledReqs) {
                logFile << req_id << std::endl;
            }
        }
        #endif
        // 将取消的请求ID添加到总列表中
        abortedRequests.insert(abortedRequests.end(), cancelledReqs.begin(), cancelledReqs.end());
    }
    
    // 输出被取消的请求数量
    std::cout << abortedRequests.size() << std::endl;
    
    // 输出每个被取消的请求ID
    for (int req_id : abortedRequests) {
        std::cout << req_id << std::endl;
    }
    
    // 刷新输出缓冲区
    std::cout.flush();
}

// 处理读取事件
void handle_read_events(ReadRequestManager& requestManager) {
    int n_read;
    std::cin >> n_read;
    
    
    
    // 处理每个读取事件
    for (int i = 0; i < n_read; i++) {
        int req_id, obj_id;
        std::cin >> req_id >> obj_id;
        
        // 添加读取请求
        requestManager.addReadRequest(req_id, obj_id);
    }
    
    // 执行当前时间片的所有请求
    requestManager.executeTimeSlice();
    
    std::cout.flush();
}

int main() {
    // 全局预处理
    globalPreprocessing();
    // 创建磁盘管理器
    DiskManager diskManager(N, V, freqData);
    
    // 创建对象管理器
    ObjectManager objectManager(diskManager, freqData);
    
    // 创建磁盘磁头管理器
    DiskHeadManager diskHeadManager(N, V, G, diskManager);
    
    // 创建读取请求管理器
    ReadRequestManager readRequestManager(objectManager, diskHeadManager);
    
    // 模拟时间片
    for (int t = 1; t <= T + EXTRA_TIME; t++) {
        timestamp_action();
        handle_delete_events(readRequestManager);
        handle_write_events(objectManager);
        handle_read_events(readRequestManager);
    }    
    
    return 0;
} 