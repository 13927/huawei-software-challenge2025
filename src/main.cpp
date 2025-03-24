#include <cassert>
#include <cstdlib>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <string>
#include "disk_manager.h"
#include "object_manager.h"
#include "disk_head_manager.h"
#include "read_request_manager.h"
#include "constants.h"

// 存储频率数据的结构
struct FrequencyData {
    std::vector<std::vector<int>> fre_del;  // 删除操作频率
    std::vector<std::vector<int>> fre_write; // 写入操作频率
    std::vector<std::vector<int>> fre_read;  // 读取操作频率
    
    // 初始化频率数据结构
    void initialize(int m, int sliceCount) {
        fre_del.resize(m + 1);
        fre_write.resize(m + 1);
        fre_read.resize(m + 1);
        
        for (int i = 1; i <= m; i++) {
            fre_del[i].resize(sliceCount + 1, 0);
            fre_write[i].resize(sliceCount + 1, 0);
            fre_read[i].resize(sliceCount + 1, 0);
        }
    }
};

// 全局变量
int T, M, N, V, G; // 总时间片数、标签数、磁盘数、每个磁盘的存储单元数、每个磁头每个时间片最多消耗的令牌数
FrequencyData freqData; // 频率数据
int currentTimeSlice = 0;

// 全局预处理函数
void globalPreprocessing() {
    // 读取基本参数
    std::cin >> T >> M >> N >> V >> G;
    
    // 计算分片数量
    int sliceCount = (T - 1) / FRE_PER_SLICING + 1;
    
    // 初始化频率数据结构
    freqData.initialize(M, sliceCount);
    
    // 读取删除操作频率
    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= sliceCount; j++) {
            std::cin >> freqData.fre_del[i][j];
        }
    }
    
    // 读取写入操作频率
    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= sliceCount; j++) {
            std::cin >> freqData.fre_write[i][j];
        }
    }
    
    // 读取读取操作频率
    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= sliceCount; j++) {
            std::cin >> freqData.fre_read[i][j];
        }
    }
    
    // 输出OK表示预处理完成
    std::cout << "OK" << std::endl;
    std::cout.flush();
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
    DiskManager diskManager(N, V);
    
    // 创建对象管理器
    ObjectManager objectManager(diskManager);
    
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