#include "frequency_data.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>  // 为std::shuffle和std::mt19937添加头文件
#include <iostream>
#include <fstream>
FrequencyData::FrequencyData() : tagCount(0), sliceCount(0), totalTimeSlices(0), 
                 diskCount(0), unitsPerDisk(0), maxTokensPerSlice(0) {}

void FrequencyData::initialize(int m, int sliceCount) {
    tagCount = m;
    this->sliceCount = sliceCount;
    
    fre_del.resize(m + 1);
    fre_write.resize(m + 1);
    fre_read.resize(m + 1);
    
    for (int i = 1; i <= m; i++) {
        fre_del[i].resize(sliceCount + 1, 0);
        fre_write[i].resize(sliceCount + 1, 0);
        fre_read[i].resize(sliceCount + 1, 0);
    }
}

void FrequencyData::setSystemParameters(int t, int n, int v, int g) {
    totalTimeSlices = t;
    diskCount = n;
    unitsPerDisk = v;
    maxTokensPerSlice = g;
}

std::vector<std::vector<int>>& FrequencyData::getDeleteFrequency() {
    return fre_del;
}

std::vector<std::vector<int>>& FrequencyData::getWriteFrequency() {
    return fre_write;
}

std::vector<std::vector<int>>& FrequencyData::getReadFrequency() {
    return fre_read;
}


void FrequencyData::calculatePeakStorageNeeds() {
    peakStorageNeeds.resize(tagCount + 1, 0);
    
    for (int tag = 1; tag <= tagCount; tag++) {
        int currentStorage = 0;
        int maxStorage = 0;
        
        for (int slice = 1; slice <= sliceCount; slice++) {
            // 累加写入操作
            currentStorage += fre_write[tag][slice];
            // 减去删除操作
            currentStorage -= fre_del[tag][slice];
            // 更新最大值
            maxStorage = std::max(maxStorage, currentStorage);
        }
        
        peakStorageNeeds[tag] = maxStorage;
    }
}

void FrequencyData::calculateTagCorrelation() {
    // 计算每个标签在每个时间片的读取概率
    readRatios.resize(tagCount + 1, std::vector<double>(sliceCount + 1, 0.0));
    
    for (int tag = 1; tag <= tagCount; tag++) {
        int currentStorage = 0;
        for (int slice = 1; slice <= sliceCount; slice++) {
            currentStorage += fre_write[tag][slice];
            currentStorage -= fre_del[tag][slice];

            readRatios[tag][slice] = static_cast<double>(fre_read[tag][slice]) / currentStorage;

        }
    }

    #ifndef NDEBUG
    //输出readRatios到cerr
    std::cerr << "=== 标签读取概率 ===" << std::endl;
    for (int tag = 1; tag <= tagCount; tag++) {
        std::cerr << "标签 " << tag << " 的读取概率:" << std::endl;
        for (int slice = 1; slice <= sliceCount; slice++) {
            std::cerr << "时间片 " << slice << ": " << readRatios[tag][slice] << std::endl;
        }
        std::cerr << std::endl;
    }
    #endif

    tagCorrelation.resize(tagCount + 1, std::vector<double>(tagCount + 1, 0.0));
    
    // 预计算 normX
    std::vector<double> norms(tagCount + 1, 0.0);
    for (int i = 1; i <= tagCount; i++) {
        for (int slice = 1; slice <= sliceCount; slice++) {
            double readProbX = readRatios[i][slice];
            if (std::isfinite(readProbX)) {
                norms[i] += readProbX * readProbX;
            }
        }
        norms[i] = sqrt(norms[i]); // 直接预计算 sqrt()
    }

    // 计算余弦相似度
    for (int i = 1; i <= tagCount; i++) {
        for (int j = i + 1; j <= tagCount; j++) {
            if (norms[i] == 0 || norms[j] == 0) { 
                tagCorrelation[i][j] = tagCorrelation[j][i] = 0.0;
                continue; // 避免除以 0
            }

            double dotProduct = 0.0;
            for (int slice = 1; slice <= sliceCount; slice++) {
                double readProbX = readRatios[i][slice];
                double readProbY = readRatios[j][slice];

                if (std::isfinite(readProbX) && std::isfinite(readProbY)) {
                    dotProduct += readProbX * readProbY;
                }
            }

            tagCorrelation[i][j] = tagCorrelation[j][i] = dotProduct / (norms[i] * norms[j]); 
        }
    }
    
    // 对标签相关性排序
    sortTagCorrelation();
}

void FrequencyData::sortTagCorrelation() {
    // 清空原有排序结果
    sortedTagCorrelation.clear();
    
    // 为每个标签创建按相关性排序的相关标签列表
    for (int i = 1; i <= tagCount; i++) {
        std::vector<std::pair<int, double>> correlations;
        
        // 收集当前标签与所有其他标签的相关性
        for (int j = 1; j <= tagCount; j++) {
            if (i != j) {  // 排除自身
                correlations.push_back({j, tagCorrelation[i][j]});
            }
        }
        
        // 按相关性从高到低排序
        std::sort(correlations.begin(), correlations.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // 存储排序结果
        sortedTagCorrelation[i] = correlations;
    }
    
    #ifndef NDEBUG
    // 输出排序后的相关性
    std::cerr << "=== 排序后的标签相关性 ===" << std::endl;
    for (int tag = 1; tag <= tagCount; tag++) {
        std::cerr << "标签 " << tag << " 的相关标签排序:" << std::endl;
        const auto& correlations = sortedTagCorrelation[tag];
        for (size_t i = 0; i < correlations.size(); i++) {
            std::cerr << "  - 标签 " << correlations[i].first 
                    << ": 相关性=" << correlations[i].second << std::endl;
        }
        std::cerr << std::endl;
    }
    #endif
}

void FrequencyData::analyzeAndPreallocate() {
    // 计算峰值存储需求
    calculatePeakStorageNeeds();
    
    // 计算标签相关性
    calculateTagCorrelation();
    
    // 计算总峰值存储需求
    int totalPeakStorage = std::accumulate(peakStorageNeeds.begin(), peakStorageNeeds.end(), 0);
    
    // 计算每个标签应分配的总存储单元数
    tagTotalUnits.resize(tagCount + 1, 0);
    for (int tag = 1; tag <= tagCount; tag++) {
        // 根据峰值存储需求分配基础单元
        double storageRatio = static_cast<double>(peakStorageNeeds[tag]) / totalPeakStorage;
        int baseUnits = static_cast<int>(storageRatio * (diskCount * unitsPerDisk));
        
        // 直接使用基础单元作为分配单元数，不再考虑读取优先级
        tagTotalUnits[tag] = baseUnits;
    }
    
    // 确保总分配不超过系统容量
    int totalAllocated = std::accumulate(tagTotalUnits.begin(), tagTotalUnits.end(), 0);
    if (totalAllocated > diskCount * unitsPerDisk) {
        double scaleFactor = static_cast<double>(diskCount * unitsPerDisk) / totalAllocated;
        for (int tag = 1; tag <= tagCount; tag++) {
            tagTotalUnits[tag] = static_cast<int>(tagTotalUnits[tag] * scaleFactor);
        }
    }

    //平均分配，得分略降低 65.4297
    // for (int tag = 1; tag <= 16; tag++)
    //     tagTotalUnits[tag] = 1;
    
    #ifndef NDEBUG
    {
    // 将分析结果写入文件
    std::ofstream outFile("storage_analysis.txt");
    if (!outFile.is_open()) {
        std::cerr << "无法创建分析结果文件" << std::endl;
        return;
    }
    
    // 写入基本统计信息
    outFile << "=== 存储分析报告 ===\n\n";
    outFile << "标签数量: " << tagCount << "\n";
    outFile << "切片数量: " << sliceCount << "\n";
    outFile << "磁盘数量: " << diskCount << "\n";
    outFile << "每个磁盘存储单元数: " << unitsPerDisk << "\n";
    outFile << "总系统容量: " << (diskCount * unitsPerDisk) << " 单元\n\n";
    
    // 写入每个标签的存储分配情况
    outFile << "=== 标签存储分配 ===\n";
    for (int tag = 1; tag <= tagCount; tag++) {
        outFile << "标签 " << tag << ":\n";
        outFile << "  峰值存储需求: " << peakStorageNeeds[tag] << " 单元\n";
        outFile << "  分配总单元数: " << tagTotalUnits[tag] << " 单元\n\n";
    }
    
    // 写入标签相关性矩阵
    outFile << "=== 标签相关性矩阵 ===\n";
    for (int i = 1; i <= tagCount; i++) {
        for (int j = 1; j <= tagCount; j++) {
            outFile << std::fixed << std::setprecision(2) << tagCorrelation[i][j] << " ";
        }
        outFile << "\n";
    }
    outFile << "\n";

    // 为每个标签写入相关性排序
    outFile << "\n=== 每个标签的相关性排序 ===\n";
    for (int i = 1; i <= tagCount; i++) {
        outFile << "标签 " << i << " 的相关性排序:\n";
        
        // 使用已排序的相关性数据
        const auto& correlations = sortedTagCorrelation[i];
        
        // 输出排序结果
        for (const auto& [tag, corr] : correlations) {
            outFile << "  与标签 " << tag << ": " 
                   << std::fixed << std::setprecision(3) << corr << "\n";
        }
        outFile << "\n";
    }
    
    outFile.close();
    }
    #endif
    
    // 分配标签到磁盘单元
    allocateTagsToDiskUnits();
}

void FrequencyData::allocateTagsToDiskUnits() {
    // 清空之前的分配结果
    diskAllocationResult.clear();
    tagAllocationResult.clear();
    
    // 每个磁盘已分配的单元数
    std::vector<int> diskAllocated(diskCount + 1, 0);
    
    // 计算总系统容量
    int totalSystemUnits = diskCount * unitsPerDisk;
    
    // 定义页大小 - 可以根据需要调整 4754 = 2x3x7x137
    const int PAGE_SIZE = 21; // 
    
    // 计算每个磁盘的页数
    int pagesPerDisk = unitsPerDisk / PAGE_SIZE;
    
    // 计算总页数
    int totalPages = pagesPerDisk * diskCount;
    
    // 计算每个标签需要分配的总页数
    std::vector<int> tagTotalPages(tagCount + 1, 0);
    
    // 计算总峰值存储需求
    int totalPeakNeeds = 0;
    for (int tag = 1; tag <= tagCount; tag++) {
        totalPeakNeeds += peakStorageNeeds[tag];
    }
    
    // 根据峰值存储需求分配页数
    for (int tag = 1; tag <= tagCount; tag++) {
        double storageRatio = (double)peakStorageNeeds[tag] / (double)totalPeakNeeds;
        tagTotalPages[tag] = std::round(storageRatio * totalPages);
        
        // 确保每个标签至少有3页（对应3个副本）
        tagTotalPages[tag] = std::max(3, tagTotalPages[tag]);
        
        // 确保每个标签在每个磁盘上至少有1页
        tagTotalPages[tag] = std::max(diskCount, tagTotalPages[tag]);
    }
    
    // 调整总分配页数，确保不超过系统容量
    int totalAllocatedPages = 0;
    for (int tag = 1; tag <= tagCount; tag++) {
        totalAllocatedPages += tagTotalPages[tag];
    }
    
    if (totalAllocatedPages > totalPages) {
        double scale = static_cast<double>(totalPages) / totalAllocatedPages;
        for (int tag = 1; tag <= tagCount; tag++) {
            tagTotalPages[tag] = std::max(diskCount, static_cast<int>(tagTotalPages[tag] * scale));
        }
    }
    // std::cerr << "totalAllocatedPages: " << totalAllocatedPages << std::endl;
    
    #ifndef NDEBUG
    std::ofstream diskDebugFile("page_allocation_debug.txt");
    if (diskDebugFile.is_open()) {
        diskDebugFile << "=== 页分配调试信息 ===\n\n";
        diskDebugFile << "标签总数: " << tagCount << "\n";
        diskDebugFile << "磁盘总数: " << diskCount << "\n";
        diskDebugFile << "每个磁盘单元数: " << unitsPerDisk << "\n";
        diskDebugFile << "页大小: " << PAGE_SIZE << " 单元\n";
        diskDebugFile << "每个磁盘页数: " << pagesPerDisk << "\n";
        diskDebugFile << "总页数: " << totalPages << "\n\n";
        
        diskDebugFile << "标签页分配情况:\n";
        for (int tag = 1; tag <= tagCount; tag++) {
            diskDebugFile << "标签 " << tag << ": " << tagTotalPages[tag] << " 页\n";
        }
        diskDebugFile << "\n";
    }
    #endif
    
    // 创建标签顺序 - 按峰值存储需求从大到小排序
    std::vector<std::pair<int, int>> sortedTagsByStorage;
    for (int tag = 1; tag <= tagCount; tag++) {
        sortedTagsByStorage.push_back({tag, peakStorageNeeds[tag]});
    }
    std::sort(sortedTagsByStorage.begin(), sortedTagsByStorage.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // 提取排序后的标签ID
    std::vector<int> sortedTags;
    for (const auto& pair : sortedTagsByStorage) {
        sortedTags.push_back(pair.first);
    }
    // std::cerr << "sortedTags: " << sortedTags.size() << std::endl;
    
    // 为每个标签在每个磁盘上分配页
    std::vector<std::vector<int>> tagPagesPerDisk(tagCount + 1, std::vector<int>(diskCount + 1, 0));
    
    // 第一步：确保每个标签在每个磁盘上至少有1页
    for (int tag = 1; tag <= tagCount; tag++) {
        for (int disk = 1; disk <= diskCount; disk++) {
            tagPagesPerDisk[tag][disk] = 1;
            tagTotalPages[tag]--;
        }
    }
    
    // 第二步：均匀分配剩余页，确保每个标签在不同磁盘上的页数大致相等
    // 按磁盘循环分配，直到所有标签的页数都分配完毕
    int currentDisk = 1;
    for (int tag : sortedTags) {
        while (tagTotalPages[tag] > 0) {
            tagPagesPerDisk[tag][currentDisk]++;
            tagTotalPages[tag]--;
            
            // 移动到下一个磁盘
            currentDisk++;
            if (currentDisk > diskCount) {
                currentDisk = 1;
            }
        }
    }

    #ifndef NDEBUG
    std::ofstream diskDebugFile("page_allocation_debug.txt");
    if (diskDebugFile.is_open()) {
        diskDebugFile << "每个标签在每个磁盘上的页数:\n";
        for (int tag = 1; tag <= tagCount; tag++) {
            diskDebugFile << "标签 " << tag << ":\n";
            for (int disk = 1; disk <= diskCount; disk++) {
                diskDebugFile << "  磁盘 " << disk << ": " << tagPagesPerDisk[tag][disk] << " 页\n";
            }
            diskDebugFile << "\n";
        }
    }
    // 输出每个磁盘的分配情况
    diskDebugFile << "\n每个磁盘的分配情况:\n";
    for (int disk = 1; disk <= diskCount; disk++) {
        int totalPagesOnDisk = 0;
        diskDebugFile << "磁盘 " << disk << ":\n";
        
        // 计算该磁盘上所有标签的总页数
        for (int tag = 1; tag <= tagCount; tag++) {
            totalPagesOnDisk += tagPagesPerDisk[tag][disk];
        }
        
        diskDebugFile << "  总页数: " << totalPagesOnDisk << " / " << pagesPerDisk << "\n";
        diskDebugFile << "  使用率: " << (totalPagesOnDisk * 100.0 / pagesPerDisk) << "%\n";
        
        // 显示每个标签在该磁盘上的页数
        diskDebugFile << "  标签分布:\n";
        for (int tag = 1; tag <= tagCount; tag++) {
            if (tagPagesPerDisk[tag][disk] > 0) {
                diskDebugFile << "    标签 " << tag << ": " << tagPagesPerDisk[tag][disk] << " 页 (" 
                              << (tagPagesPerDisk[tag][disk] * 100.0 / totalPagesOnDisk) << "%)\n";
            }
        }
        diskDebugFile << "\n";
    }
    #endif
    
    // 第三步：交替分配页面到具体的磁盘位置
    // 为每个磁盘创建一个交替排列的标签顺序
    std::vector<std::vector<int>> diskTagOrder(diskCount + 1);
    
    for (int disk = 1; disk <= diskCount; disk++) {
        // 创建一个基于磁盘ID的交替排序
        diskTagOrder[disk].resize(sortedTags.size());
        
        // 使用轮转方式创建交替排序
        // 例如：对于磁盘1，顺序是 [1,2,3,4...]
        //      对于磁盘2，顺序是 [2,3,4,...,1]
        //      对于磁盘3，顺序是 [3,4,...,1,2]
        // 这样可以确保相邻的磁盘上的标签分布是交错的
        int offset = (disk - 1) % sortedTags.size();
        for (size_t i = 0; i < sortedTags.size(); i++) {
            int idx = (i + offset) % sortedTags.size();
            diskTagOrder[disk][i] = sortedTags[idx];
        }
    }
    
    // 跟踪每个磁盘当前分配的页位置
    std::vector<int> diskCurrentPage(diskCount + 1, 0);
    
    // 跟踪每个标签在每个磁盘上已分配的页数
    std::vector<std::vector<int>> tagAllocatedPages(tagCount + 1, std::vector<int>(diskCount + 1, 0));
    
    // 按照交替顺序分配页面
    // 使用轮转分配方式，确保同一磁盘上不同标签的页面交替出现
    for (int disk = 1; disk <= diskCount; disk++) {
        // 初始化当前页位置
        diskCurrentPage[disk] = 0;
        
        // 创建循环计数器，控制循环次数
        int cycleCount = 0;
        int maxCycles = pagesPerDisk * 2; // 设置最大循环次数，避免无限循环
        
        // 记录标签轮转顺序
        std::vector<int> tagRotation = diskTagOrder[disk];
        int currentTagIndex = 0;
        
        // 循环直到磁盘填满或达到最大循环次数
        while (diskCurrentPage[disk] < pagesPerDisk && cycleCount < maxCycles) {
            // 获取当前要分配的标签
            int currentTag = tagRotation[currentTagIndex];
            
            // 如果该标签在这个磁盘上还有页需要分配
            if (tagAllocatedPages[currentTag][disk] < tagPagesPerDisk[currentTag][disk]) {
                // 分配一页给当前标签
                int startUnit = diskCurrentPage[disk] * PAGE_SIZE + 1;
                int endUnit = startUnit + PAGE_SIZE - 1;
                
                // 记录分配结果
                DiskRange range = {startUnit, endUnit, currentTag};
                diskAllocationResult[disk].push_back(range);
                
                // 同时记录到标签分配结果
                tagAllocationResult[currentTag].push_back(std::make_tuple(disk, startUnit, endUnit));
                
                // 更新已分配计数
                tagAllocatedPages[currentTag][disk]++;
                diskCurrentPage[disk]++;
            }
            
            // 移动到下一个标签
            currentTagIndex = (currentTagIndex + 1) % tagRotation.size();
            
            // 增加循环计数
            cycleCount++;
        }
    }
    
    // 检查是否所有标签都已完全分配
    bool fullyAllocated = true;
    for (int tag = 1; tag <= tagCount; tag++) {
        for (int disk = 1; disk <= diskCount; disk++) {
            if (tagAllocatedPages[tag][disk] < tagPagesPerDisk[tag][disk]) {
                fullyAllocated = false;
                std::cerr << "警告: 标签 " << tag << " 在磁盘 " << disk 
                          << " 上只分配了 " << tagAllocatedPages[tag][disk] 
                          << " 页，而不是需要的 " << tagPagesPerDisk[tag][disk] << " 页。" << std::endl;
            }
        }
    }
    
    // // 在记录最终结果前，合并每个磁盘上相邻的同标签页面
    // std::map<int, std::vector<DiskRange>> mergedDiskAllocation;
    // std::map<int, std::vector<std::tuple<int, int, int>>> mergedTagAllocation;
    
    // for (int disk = 1; disk <= diskCount; disk++) {
    //     if (diskAllocationResult.find(disk) == diskAllocationResult.end()) continue;
        
    //     // 按照起始位置排序，确保正确合并
    //     std::sort(diskAllocationResult[disk].begin(), diskAllocationResult[disk].end(), 
    //              [](const DiskRange& a, const DiskRange& b) { return a.startUnit < b.startUnit; });
        
    //     std::vector<DiskRange> mergedRanges;
        
    //     // 如果没有分配，则跳过
    //     if (diskAllocationResult[disk].empty()) continue;
        
    //     // 初始化第一个范围
    //     DiskRange currentRange = diskAllocationResult[disk][0];
        
    //     // 合并相邻的同标签范围
    //     for (size_t i = 1; i < diskAllocationResult[disk].size(); i++) {
    //         const DiskRange& nextRange = diskAllocationResult[disk][i];
            
    //         // 如果标签相同且范围相邻，则合并
    //         if (nextRange.tag == currentRange.tag && nextRange.startUnit == currentRange.endUnit + 1) {
    //             // 扩展当前范围到包含下一个范围
    //             currentRange.endUnit = nextRange.endUnit;
    //         } else {
    //             // 否则，添加当前范围到结果中，并开始新的范围
    //             mergedRanges.push_back(currentRange);
    //             currentRange = nextRange;
    //         }
    //     }
        
    //     // 添加最后一个范围
    //     mergedRanges.push_back(currentRange);
        
    //     // 更新磁盘分配结果
    //     mergedDiskAllocation[disk] = mergedRanges;
        
    //     // 更新标签分配结果
    //     for (const auto& range : mergedRanges) {
    //         if (range.tag < 0) continue; // 跳过无效标签
            
    //         mergedTagAllocation[range.tag].push_back(
    //             std::make_tuple(disk, range.startUnit, range.endUnit));
    //     }
    // }
    
    // // 用合并后的结果替换原始结果
    // diskAllocationResult = mergedDiskAllocation;
    // tagAllocationResult = mergedTagAllocation;
    
    #ifndef NDEBUG
    if (diskDebugFile.is_open()) {
        diskDebugFile << "\n=== 最终分配结果 ===\n";
        
        for (int disk = 1; disk <= diskCount; disk++) {
            diskDebugFile << "磁盘 " << disk << " 分配情况:\n";
            for (const auto& range : diskAllocationResult[disk]) {
                diskDebugFile << "  [" << range.startUnit << "-" << range.endUnit 
                         << "] -> 标签 " << range.tag << "\n";
            }
            diskDebugFile << "\n";
        }
        
        diskDebugFile.close();
    }
    #endif
    
    // // 分配虚拟标签0（用于未分配空间）
    // std::vector<std::tuple<int, int, int>> tag0Allocations;
    
    // for (int disk = 1; disk <= diskCount; disk++) {
    //     // 收集已分配范围
    //     std::vector<std::pair<int, int>> allocatedRanges;
    //     for (const auto& range : diskAllocationResult[disk]) {
    //         allocatedRanges.push_back({range.startUnit, range.endUnit});
    //     }
        
    //     // 按起始位置排序
    //     std::sort(allocatedRanges.begin(), allocatedRanges.end());
        
    //     // 查找未分配范围
    //     int currentPos = 1;
    //     for (const auto& [start, end] : allocatedRanges) {
    //         if (start > currentPos) {
    //             // 找到未分配区域
    //             DiskRange range = {currentPos, start - 1, 0}; // 标签0表示未分配
    //             diskAllocationResult[disk].push_back(range);
    //             tag0Allocations.push_back(std::make_tuple(disk, currentPos, start - 1));
    //         }
    //         currentPos = end + 1;
    //     }
        
    //     // 检查磁盘末尾是否有未分配空间
    //     if (currentPos <= unitsPerDisk) {
    //         DiskRange range = {currentPos, unitsPerDisk, 0};
    //         diskAllocationResult[disk].push_back(range);
    //         tag0Allocations.push_back(std::make_tuple(disk, currentPos, unitsPerDisk));
    //     }
    // }
    
    // // 将标签0的分配结果添加到tagAllocationResult
    // if (!tag0Allocations.empty()) {
    //     tagAllocationResult[0] = tag0Allocations;
    // }
}

// 实现查询接口
std::vector<std::tuple<int, int>> FrequencyData::getTagRangesOnDisk(int tag, int diskId) const {
    std::vector<std::tuple<int, int>> ranges;
    
    auto diskIt = diskAllocationResult.find(diskId);
    if (diskIt != diskAllocationResult.end()) {
        for (const auto& range : diskIt->second) {
            if (range.tag == tag) {
                ranges.push_back(std::make_tuple(range.startUnit, range.endUnit));
            }
        }
    }
    
    return ranges;
}

std::vector<std::tuple<int, int, int>> FrequencyData::getTagAllocation(int tag) const {
    auto it = tagAllocationResult.find(tag);
    if (it != tagAllocationResult.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::tuple<int, int, int>> FrequencyData::getDiskAllocation(int diskId) const {
    std::vector<std::tuple<int, int, int>> allocation;
    
    auto it = diskAllocationResult.find(diskId);
    if (it != diskAllocationResult.end()) {
        for (const auto& range : it->second) {
            allocation.push_back(std::make_tuple(range.startUnit, range.endUnit, range.tag));
        }
    }
    
    return allocation;
}

int FrequencyData::getTagDiskCount(int tag) const {
    auto it = tagAllocationResult.find(tag);
    if (it != tagAllocationResult.end()) {
        return it->second.size();
    }
    return 0;
}

int FrequencyData::getTagTotalAllocatedUnits(int tag) const {
    int total = 0;
    auto it = tagAllocationResult.find(tag);
    if (it != tagAllocationResult.end()) {
        for (const auto& [disk, start, end] : it->second) {
            total += (end - start + 1);
        }
    }
    return total;
}

std::vector<std::pair<int, double>> FrequencyData::getRelatedTags(int tag, int limit) const {
    std::vector<std::pair<int, double>> result;
    
    auto it = sortedTagCorrelation.find(tag);
    if (it != sortedTagCorrelation.end()) {
        const auto& correlations = it->second;
        if (limit <= 0 || limit > static_cast<int>(correlations.size())) {
            // 返回所有相关标签
            return correlations;
        } else {
            // 返回限制数量的相关标签
            return std::vector<std::pair<int, double>>(correlations.begin(), correlations.begin() + limit);
        }
    }
    
    return result;
}

double FrequencyData::getTagCorrelation(int tag1, int tag2) const {
    if (tag1 >= 1 && tag1 <= tagCount && tag2 >= 1 && tag2 <= tagCount) {
        return tagCorrelation[tag1][tag2];
    }
    return 0.0;
} 