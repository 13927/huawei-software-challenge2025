#ifndef FREQUENCY_DATA_H
#define FREQUENCY_DATA_H

#include <vector>
#include <utility>
#include <map>
#include <algorithm>
#include <iostream>
#include <cmath>
#include "object_manager.h"
#include "constants.h"
#include <tuple>


// 存储和分析频率数据的类
class FrequencyData {
private:
    std::vector<std::vector<int>> fre_del;  // 删除操作对象大小之和
    std::vector<std::vector<int>> fre_write; // 写入操作对象大小之和
    std::vector<std::vector<int>> fre_read;  // 读取操作对象大小之和
    
    int tagCount;               // 标签数量
    int sliceCount;             // 分片数量
    double totalTimeSlices;      // 总时间片数
    int diskCount;              // 磁盘数量
    int unitsPerDisk;           // 每个磁盘的存储单元数
    int maxTokensPerSlice;      // 每个时间片最大令牌数

    std::vector<int> peakStorageNeeds;        // 每个标签的峰值存储需求
    std::vector<std::vector<double>> readRatios;
    std::vector<std::vector<double>> tagCorrelation;  // 标签间的读取相关性
    std::map<int, std::vector<std::pair<int, double>>> sortedTagCorrelation; // 按相关性从大到小排序的标签 <tag_id, [(related_tag_id, correlation), ...]>

    
    std::vector<int> tagTotalUnits;           // 每个标签应分配的总存储单元数

    // 存储最终分配结果的数据结构
    struct DiskRange {
        int startUnit;    // 起始单元
        int endUnit;      // 结束单元
        int tag;          // 标签ID
    };
    
    // 磁盘分配结果 (disk_id -> ranges)
    std::map<int, std::vector<DiskRange>> diskAllocationResult;
    
    // 标签分配结果 (tag -> [(disk_id, start_unit, end_unit)])
    std::map<int, std::vector<std::tuple<int, int, int>>> tagAllocationResult;

    // 新增私有方法
    void calculatePeakStorageNeeds();         // 计算峰值存储需求
    void calculateTagCorrelation();           // 计算标签间的读取相关性
    void sortTagCorrelation();                // 排序标签相关性
    void calculateStorageNeeds();             // 计算存储需求
    void allocateTagsToDiskUnits();

public:
    FrequencyData();
    
    // 初始化频率数据结构
    void initialize(int m, int sliceCount);
    
    // 设置系统参数
    void setSystemParameters(int t, int n, int v, int g);
    
    // 获取删除频率数据
    std::vector<std::vector<int>>& getDeleteFrequency();
    
    // 获取写入频率数据
    std::vector<std::vector<int>>& getWriteFrequency();
    
    // 获取读取频率数据
    std::vector<std::vector<int>>& getReadFrequency();
    
    // 分析数据并预分配空间
    void analyzeAndPreallocate();
    
    // 查询接口
    // 获取标签在特定磁盘上的分配区间 <startUnit, endUnit>
    std::vector<std::tuple<int, int>> getTagRangesOnDisk(int tag, int diskId) const;
    
    // 获取标签在所有磁盘上的分配情况 <disk_id, start_unit, end_unit>
    std::vector<std::tuple<int, int, int>> getTagAllocation(int tag) const;
    
    // 获取特定磁盘上的所有分配区间 <startUnit, endUnit, Tag>[]
    std::vector<std::tuple<int, int, int>> getDiskAllocation(int diskId) const;
    
    // 获取标签分配的磁盘数量
    int getTagDiskCount(int tag) const;
    
    // 获取标签总分配空间
    int getTagTotalAllocatedUnits(int tag) const;

    // 获取标签总数
    int getTagCount() const { return tagCount; }
    
    // 获取与指定标签相关性最高的标签列表
    std::vector<std::pair<int, double>> getRelatedTags(int tag, int limit = -1) const;
    
    // 获取两个标签之间的相关性
    double getTagCorrelation(int tag1, int tag2) const;
};

#endif // FREQUENCY_DATA_H 