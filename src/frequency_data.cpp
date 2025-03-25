#include "frequency_data.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <numeric>

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
    tagCorrelation.resize(tagCount + 1, std::vector<double>(tagCount + 1, 0.0));
    
    for (int i = 1; i <= tagCount; i++) {
        for (int j = i + 1; j <= tagCount; j++) {
            double correlation = 0.0;
            double totalReads = 0.0;
            
            // 计算两个标签的读取相关性
            for (int slice = 1; slice <= sliceCount; slice++) {
                correlation += std::min(fre_read[i][slice], fre_read[j][slice]);
                totalReads += std::max(fre_read[i][slice], fre_read[j][slice]);
            }
            
            // 归一化相关性
            if (totalReads > 0) {
                correlation = correlation / totalReads;
            }
            
            tagCorrelation[i][j] = correlation;
            tagCorrelation[j][i] = correlation;
        }
    }
}

void FrequencyData::calculateTagPriorities() {
    tagPriorities.clear();

    std::vector<double> tagTotalReads(tagCount + 1, 0.0);
    std::vector<double> tagPeakReads(tagCount + 1, 0.0);
    std::vector<double> tagVariances(tagCount + 1, 0.0);
    
    #ifndef NDEBUG
    // 创建一个调试输出文件
    std::ofstream debugFile("tag_priorities_debug.txt");
    if (!debugFile.is_open()) {
        std::cerr << "无法创建调试文件" << std::endl;
    } else {
        debugFile << "=== 标签优先级调试信息 ===\n\n";
        debugFile << "标签数量: " << tagCount << "\n";
        debugFile << "切片数量: " << sliceCount << "\n\n";
    }
    #endif
    
    for (int tag = 1; tag <= tagCount; tag++) {
        // 计算总读取量
        double totalReads = 0.0;
        double maxReadsInSlice = 0.0;
        double readVariance = 0.0; // 读取量的方差（表示读取的波动性）
        
        #ifndef NDEBUG
        if (debugFile.is_open()) {
            debugFile << "标签 " << tag << " 读取频率详情:\n";
            debugFile << "  切片\t读取量\n";
        }
        #endif
        
        // 计算平均读取量和最大读取量
        for (int slice = 1; slice <= sliceCount; slice++) {
            totalReads += fre_read[tag][slice];
            maxReadsInSlice = std::max(maxReadsInSlice, static_cast<double>(fre_read[tag][slice]));
            
            #ifndef NDEBUG
            if (debugFile.is_open()) {
                debugFile << "  " << slice << "\t" << fre_read[tag][slice] << "\n";
            }
            #endif
        }
        double avgReads = totalReads / sliceCount;
        
        // 计算读取量的方差（表示读取的波动性）
        for (int slice = 1; slice <= sliceCount; slice++) {
            double diff = fre_read[tag][slice] - avgReads;
            readVariance += diff * diff;
        }
        readVariance /= sliceCount;

        // 存储数据用于归一化
        tagTotalReads[tag] = totalReads;
        tagPeakReads[tag] = maxReadsInSlice;
        tagVariances[tag] = readVariance;
        
        #ifndef NDEBUG
        if (debugFile.is_open()) {
            debugFile << "  总读取量: " << totalReads << "\n";
            debugFile << "  平均读取量: " << avgReads << "\n";
            debugFile << "  最大读取量: " << maxReadsInSlice << "\n";
            debugFile << "  读取方差: " << readVariance << "\n\n";
        }
        #endif
    }

    // 计算最大值用于归一化
    double maxTotalReads = *std::max_element(tagTotalReads.begin(), tagTotalReads.end());
    double maxPeakReads = *std::max_element(tagPeakReads.begin(), tagPeakReads.end());
    double maxVariance = *std::max_element(tagVariances.begin(), tagVariances.end());
    
    #ifndef NDEBUG
    if (debugFile.is_open()) {
        debugFile << "归一化参数:\n";
        debugFile << "  最大总读取量: " << maxTotalReads << "\n";
        debugFile << "  最大峰值读取量: " << maxPeakReads << "\n";
        debugFile << "  最大方差: " << maxVariance << "\n\n";
        
        debugFile << "标签优先级计算:\n";
        debugFile << "标签\t总读取\t峰值读取\t稳定性\t优先级\n";
    }
    #endif
    
    for (int tag = 1; tag <= tagCount; tag++) {
        // 归一化
        double totalReadsScore = maxTotalReads > 0 ? (tagTotalReads[tag] / maxTotalReads) : 0;
        double peakReadsScore = maxPeakReads > 0 ? (tagPeakReads[tag] / maxPeakReads) : 0;
        double stabilityScore = maxVariance > 0 ? (1.0 - (tagVariances[tag] / maxVariance)) : 1.0;

        // 计算优先级
        double priority = 0.45 * totalReadsScore + 
                          0.35 * peakReadsScore + 
                          0.2 * stabilityScore;

        priority = std::max(0.0, std::min(1.0, priority)); // 确保在 [0,1]
        
        #ifndef NDEBUG
        if (debugFile.is_open()) {
            debugFile << tag << "\t" 
                     << totalReadsScore << "\t" 
                     << peakReadsScore << "\t" 
                     << stabilityScore << "\t" 
                     << priority << "\n";
        }
        #endif

        tagPriorities.push_back(std::make_pair(tag, priority));
    }

}

void FrequencyData::analyzeAndPreallocate() {
    // 计算峰值存储需求
    calculatePeakStorageNeeds();
    
    // 计算标签相关性
    calculateTagCorrelation();
    
    // 计算标签优先级
    calculateTagPriorities();
    
    // 计算总峰值存储需求
    int totalPeakStorage = std::accumulate(peakStorageNeeds.begin(), peakStorageNeeds.end(), 0);
    
    // 计算每个标签应分配的总存储单元数
    tagTotalUnits.resize(tagCount + 1, 0);
    for (int tag = 1; tag <= tagCount; tag++) {
        // 根据峰值存储需求分配基础单元
        double storageRatio = static_cast<double>(peakStorageNeeds[tag]) / totalPeakStorage;
        int baseUnits = static_cast<int>(storageRatio * (diskCount * unitsPerDisk));
        
        // 根据读取优先级增加额外单元
        double readPriority = 0.0;
        for (const auto& pair : tagPriorities) {
            if (pair.first == tag) {
                readPriority = pair.second;
                break;
            }
        }
        int extraUnits = static_cast<int>(baseUnits * readPriority * 0.5); // 读取优先级高的标签获得更多单元
        
        tagTotalUnits[tag] = baseUnits + extraUnits;
    }
    
    // 确保总分配不超过系统容量
    int totalAllocated = std::accumulate(tagTotalUnits.begin(), tagTotalUnits.end(), 0);
    if (totalAllocated > diskCount * unitsPerDisk) {
        double scaleFactor = static_cast<double>(diskCount * unitsPerDisk) / totalAllocated;
        for (int tag = 1; tag <= tagCount; tag++) {
            tagTotalUnits[tag] = static_cast<int>(tagTotalUnits[tag] * scaleFactor);
        }
    }
    
    #ifndef NDEBUG
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
        outFile << "  读取优先级: ";
        
        // 找到对应标签的优先级
        for (const auto& pair : tagPriorities) {
            if (pair.first == tag) {
                outFile << pair.second << "\n";
                break;
            }
        }
        
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
    
    outFile.close();
    #endif
    
    // 分配标签到磁盘单元
    allocateTagsToDiskUnits();
}

void FrequencyData::allocateTagsToDiskUnits() {
    // 清空之前的分配结果
    diskAllocationResult.clear();
    tagAllocationResult.clear();
    
    // 每个磁盘已分配的单元数（注意：磁盘ID从1开始，但内部数组从0开始）
    std::vector<int> diskAllocated(diskCount + 1, 0);
    
    // 每个磁盘分配的标签和对应的单元数
    std::vector<std::vector<std::pair<int, int>>> diskAllocation(diskCount + 1);
    
    // 每个标签分配到的磁盘列表
    std::vector<std::vector<int>> tagToDiskMap(tagCount + 1);
    
    // 按读取优先级从高到低排序标签
    std::vector<int> sortedTags;
    std::vector<std::pair<int, double>> tagPriorityPairs;
    for (const auto& tagPriority : tagPriorities) {
        sortedTags.push_back(tagPriority.first);
        tagPriorityPairs.push_back(tagPriority);
    }
    
    // 根据优先级排序
    std::sort(tagPriorityPairs.begin(), tagPriorityPairs.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // 为每个标签确定分配的磁盘数量
    std::vector<int> tagDiskCount(tagCount + 1, 0);
    const int MIN_DISKS_PER_TAG = 3;  // 每个标签至少分配3个磁盘
    
    // 计算基于优先级的磁盘数量分配
    double totalPriority = 0.0;
    for (const auto& [tag, priority] : tagPriorityPairs) {
        totalPriority += priority > 0 ? priority : 0.01; // 确保每个标签至少有一些优先级
    }
    
    int extraDisksToDistribute = diskCount - MIN_DISKS_PER_TAG * tagCount;
    if (extraDisksToDistribute < 0) extraDisksToDistribute = 0;
    
    #ifndef NDEBUG
    std::ofstream diskDebugFile("disk_allocation_debug.txt");
    if (diskDebugFile.is_open()) {
        diskDebugFile << "=== 磁盘分配调试信息 ===\n\n";
        diskDebugFile << "标签数量: " << tagCount << "\n";
        diskDebugFile << "磁盘数量: " << diskCount << "\n";
        diskDebugFile << "每个磁盘单元数: " << unitsPerDisk << "\n";
        diskDebugFile << "额外可分配磁盘数: " << extraDisksToDistribute << "\n\n";
        
        diskDebugFile << "标签优先级排序:\n";
        for (const auto& [tag, priority] : tagPriorityPairs) {
            diskDebugFile << "标签 " << tag << ": 优先级=" << priority << "\n";
        }
        diskDebugFile << "\n";
    }
    #endif
    
    // 根据优先级分配额外的磁盘
    for (const auto& [tag, priority] : tagPriorityPairs) {
        // 基础分配：每个标签至少3个磁盘
        tagDiskCount[tag] = MIN_DISKS_PER_TAG;
        
        // 额外分配：根据优先级比例
        double priorityRatio = (priority > 0 ? priority : 0.01) / totalPriority;
        int extraDisks = static_cast<int>(extraDisksToDistribute * priorityRatio);
        
        // 确保不会分配超过可用的磁盘数量
        tagDiskCount[tag] += extraDisks;
        if (tagDiskCount[tag] > diskCount) {
            tagDiskCount[tag] = diskCount;
        }
        
        #ifndef NDEBUG
        if (diskDebugFile.is_open()) {
            diskDebugFile << "标签 " << tag << " 分配磁盘数: " << tagDiskCount[tag] 
                     << " (基础=" << MIN_DISKS_PER_TAG << ", 额外=" << extraDisks << ")\n";
        }
        #endif
    }
    
    // 每个标签每个磁盘分配的单元数
    std::vector<std::vector<int>> tagDiskAllocation(tagCount + 1, std::vector<int>(diskCount + 1, 0));
    
    // 计算需要分配的总单元数（系统总容量的90%）
    int totalSystemUnits = diskCount * unitsPerDisk;
    int totalUnitsToAllocate = static_cast<int>(totalSystemUnits * 0.9);
    
    // 调整每个标签的分配量，使总和为系统容量的90%
    int totalTagUnits = std::accumulate(tagTotalUnits.begin(), tagTotalUnits.end(), 0);
    if (totalTagUnits > 0) {
        double scaleFactor = static_cast<double>(totalUnitsToAllocate) / totalTagUnits;
        for (int tag = 1; tag <= tagCount; tag++) {
            tagTotalUnits[tag] = static_cast<int>(tagTotalUnits[tag] * scaleFactor);
            if (tagTotalUnits[tag] < tagDiskCount[tag]) {
                // 确保每个标签至少有足够单元分配给每个磁盘
                tagTotalUnits[tag] = tagDiskCount[tag];
            }
        }
    }
    
    // 获取每个标签需要分配的单元数
    std::vector<int> tagUnitsToBePlaced = tagTotalUnits;
    
    #ifndef NDEBUG
    if (diskDebugFile.is_open()) {
        diskDebugFile << "标签的磁盘分配和单元数:\n";
        diskDebugFile << "标签\t目标磁盘数\t单元总数\t每磁盘单元数\n";
        for (int tag = 1; tag <= tagCount; tag++) {
            int unitsPerDiskUnit = tagDiskCount[tag] > 0 ? tagTotalUnits[tag] / tagDiskCount[tag] : 0;
            diskDebugFile << tag << "\t" << tagDiskCount[tag] << "\t" 
                     << tagTotalUnits[tag] << "\t" << unitsPerDiskUnit << "\n";
        }
        diskDebugFile << "\n";
    }
    #endif
    
    // 第一步：根据计算的最佳磁盘数分配每个标签
    for (int tag : sortedTags) {
        if (tagUnitsToBePlaced[tag] <= 0 || tagDiskCount[tag] <= 0) continue;
        
        // 计算每个磁盘应该分配的单元数（均匀分配）
        int targetDisksCount = tagDiskCount[tag];
        int unitsPerTargetDisk = tagUnitsToBePlaced[tag] / targetDisksCount;
        int remainingUnits = tagUnitsToBePlaced[tag] % targetDisksCount;
        
        // 找出最空闲的N个磁盘
        std::vector<int> bestDisks;
        std::vector<std::pair<int, int>> diskLoad; // <disk_id, allocated>
        
        for (int disk = 1; disk <= diskCount; ++disk) {  // 磁盘ID从1开始
            diskLoad.push_back({disk, diskAllocated[disk]});
        }
        
        // 按已分配单元数排序
        std::sort(diskLoad.begin(), diskLoad.end(), 
                 [](const auto& a, const auto& b) { return a.second < b.second; });
        
        // 选择最空闲的targetDisksCount个磁盘
        for (int i = 0; i < targetDisksCount && i < diskLoad.size(); ++i) {
            int disk = diskLoad[i].first;
            int availableUnits = unitsPerDisk - diskAllocated[disk];
            
            if (availableUnits > 0) {
                bestDisks.push_back(disk);
            }
        }
        
        #ifndef NDEBUG
        if (diskDebugFile.is_open()) {
            diskDebugFile << "标签 " << tag << " 选择的磁盘: ";
            for (int disk : bestDisks) {
                diskDebugFile << disk << " ";
            }
            diskDebugFile << "\n";
        }
        #endif
        
        // 在找到的磁盘上分配单元
        for (size_t i = 0; i < bestDisks.size(); ++i) {
            int disk = bestDisks[i];
            
            // 计算要分配的单元数（最后一个磁盘获得剩余单元）
            int unitsToAllocate = unitsPerTargetDisk + (i == bestDisks.size() - 1 ? remainingUnits : 0);
            int availableUnits = unitsPerDisk - diskAllocated[disk];
            unitsToAllocate = std::min(unitsToAllocate, availableUnits);
            
            if (unitsToAllocate > 0) {
                tagDiskAllocation[tag][disk] = unitsToAllocate;
                diskAllocated[disk] += unitsToAllocate;
                tagUnitsToBePlaced[tag] -= unitsToAllocate;
                tagToDiskMap[tag].push_back(disk);
                
                #ifndef NDEBUG
                if (diskDebugFile.is_open()) {
                    diskDebugFile << "  分配到磁盘 " << disk << ": " << unitsToAllocate << " 单元\n";
                }
                #endif
            }
        }
    }
    
    // 第二步：处理由于磁盘空间不足而未分配的单元
    for (int tag : sortedTags) {
        if (tagUnitsToBePlaced[tag] <= 0) continue;
        
        #ifndef NDEBUG
        if (diskDebugFile.is_open()) {
            diskDebugFile << "标签 " << tag << " 仍有 " << tagUnitsToBePlaced[tag] << " 单元未分配\n";
        }
        #endif
        
        // 按剩余空间排序所有磁盘
        std::vector<std::pair<int, int>> diskSpace; // <disk_id, available_space>
        for (int disk = 1; disk <= diskCount; ++disk) {  // 磁盘ID从1开始
            int availableSpace = unitsPerDisk - diskAllocated[disk];
            if (availableSpace > 0) {
                diskSpace.push_back({disk, availableSpace});
            }
        }
        
        // 按可用空间降序排序
        std::sort(diskSpace.begin(), diskSpace.end(), 
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // 分配剩余单元到剩余空间最大的磁盘
        for (const auto& [disk, availableSpace] : diskSpace) {
            if (tagUnitsToBePlaced[tag] <= 0) break;
            
            // 检查这个磁盘是否已经分配给了这个标签
            bool alreadyAllocated = false;
            for (int allocatedDisk : tagToDiskMap[tag]) {
                if (allocatedDisk == disk) {
                    alreadyAllocated = true;
                    break;
                }
            }
            
            // 如果可以，优先分配到新磁盘
            if (!alreadyAllocated || tagToDiskMap[tag].size() < tagDiskCount[tag]) {
                int unitsToAllocate = std::min(tagUnitsToBePlaced[tag], availableSpace);
                
                tagDiskAllocation[tag][disk] += unitsToAllocate;
                diskAllocated[disk] += unitsToAllocate;
                tagUnitsToBePlaced[tag] -= unitsToAllocate;
                
                if (!alreadyAllocated) {
                    tagToDiskMap[tag].push_back(disk);
                }
                
                #ifndef NDEBUG
                if (diskDebugFile.is_open()) {
                    diskDebugFile << "  额外分配到磁盘 " << disk << ": " << unitsToAllocate << " 单元\n";
                }
                #endif
            }
        }
    }
    
    // 第三步：为每个磁盘确定标签分配的具体区间
    std::vector<std::vector<std::tuple<int, int, int>>> diskUnitRanges(diskCount + 1);
    
    for (int disk = 1; disk <= diskCount; ++disk) {  // 磁盘ID从1开始
        // 获取此磁盘上分配的所有标签
        std::vector<int> tagsOnDisk;
        for (int tag = 1; tag <= tagCount; ++tag) {
            if (tagDiskAllocation[tag][disk] > 0) {
                tagsOnDisk.push_back(tag);
            }
        }
        
        // 如果磁盘上有多个标签，根据相关性安排它们的顺序
        if (tagsOnDisk.size() > 1) {
            // 构建标签之间的相关性矩阵
            std::vector<std::vector<double>> similarityMatrix(tagsOnDisk.size(), 
                                                           std::vector<double>(tagsOnDisk.size(), 0.0));
            
            for (size_t i = 0; i < tagsOnDisk.size(); ++i) {
                for (size_t j = i+1; j < tagsOnDisk.size(); ++j) {
                    int tag1 = tagsOnDisk[i];
                    int tag2 = tagsOnDisk[j];
                    similarityMatrix[i][j] = tagCorrelation[tag1][tag2];
                    similarityMatrix[j][i] = similarityMatrix[i][j];
                }
            }
            
            // 使用贪心算法安排标签顺序，使相关性高的标签相邻
            std::vector<int> orderedTagIndices;
            std::vector<bool> visited(tagsOnDisk.size(), false);
            
            // 从第一个标签开始
            orderedTagIndices.push_back(0);
            visited[0] = true;
            
            // 依次找出与最后一个标签相关性最高的未访问标签
            while (orderedTagIndices.size() < tagsOnDisk.size()) {
                int lastIndex = orderedTagIndices.back();
                int nextIndex = -1;
                double maxSimilarity = -1.0;
                
                for (size_t i = 0; i < tagsOnDisk.size(); ++i) {
                    if (!visited[i] && similarityMatrix[lastIndex][i] > maxSimilarity) {
                        maxSimilarity = similarityMatrix[lastIndex][i];
                        nextIndex = i;
                    }
                }
                
                // 如果没有找到相关标签，选择任意未访问的标签
                if (nextIndex == -1) {
                    for (size_t i = 0; i < tagsOnDisk.size(); ++i) {
                        if (!visited[i]) {
                            nextIndex = i;
                            break;
                        }
                    }
                }
                
                orderedTagIndices.push_back(nextIndex);
                visited[nextIndex] = true;
            }
            
            // 将索引转换为实际标签
            std::vector<int> orderedTags;
            for (int idx : orderedTagIndices) {
                orderedTags.push_back(tagsOnDisk[idx]);
            }
            tagsOnDisk = orderedTags;
        }
        
        // 根据标签顺序分配具体区间
        int currentUnit = 1;  // 从1开始编号
        for (int tag : tagsOnDisk) {
            int units = tagDiskAllocation[tag][disk];
            if (units > 0) {
                int startUnit = currentUnit;
                int endUnit = startUnit + units - 1;
                
                // 存储到磁盘单元区间数组
                diskUnitRanges[disk].push_back({startUnit, endUnit, tag});
                
                // 存储到结果数据结构
                DiskRange range = {startUnit, endUnit, tag};
                diskAllocationResult[disk].push_back(range);
                
                // 同时记录到标签分配结果
                tagAllocationResult[tag].push_back(std::make_tuple(disk, startUnit, endUnit));
                
                currentUnit = endUnit + 1;
            }
        }
    }
    #ifndef NDEBUG
    // 将分配结果写入文件
    std::ofstream outFile("disk_allocation.txt");
    if (outFile.is_open()) {
        outFile << "=== 磁盘单元分配方案 ===\n\n";
        
        // 写入总体概况
        outFile << "标签数量: " << tagCount << "\n";
        outFile << "磁盘数量: " << diskCount << "\n";
        outFile << "每个磁盘单元数: " << unitsPerDisk << "\n";
        outFile << "总系统容量: " << totalSystemUnits << " 单元\n";
        
        int totalAllocatedUnits = 0;
        for (int disk = 1; disk <= diskCount; ++disk) {
            totalAllocatedUnits += diskAllocated[disk];
        }
        
        outFile << "总分配容量: " << totalAllocatedUnits
                << " 单元 (" << std::fixed << std::setprecision(2) 
                << (static_cast<double>(totalAllocatedUnits) / totalSystemUnits * 100) 
                << "%)\n\n";
        
        // 写入每个磁盘的分配详情
        for (int disk = 1; disk <= diskCount; ++disk) {  // 磁盘ID从1开始
            outFile << "磁盘 " << disk << " 分配情况:\n";
            outFile << "  总分配单元: " << diskAllocated[disk] << "/" << unitsPerDisk << " (" 
                    << std::fixed << std::setprecision(2) 
                    << (static_cast<double>(diskAllocated[disk]) / unitsPerDisk * 100) << "%)\n";
            outFile << "  单元分配区间: [起始单元, 结束单元, 标签]\n";
            
            for (const auto& [start, end, tag] : diskUnitRanges[disk]) {
                outFile << "    [" << start << "-" << end << "] -> 标签 " << tag 
                        << " (" << (end - start + 1) << " 单元)\n";
            }
            outFile << "\n";
        }
        
        // 写入每个标签的分配概况
        outFile << "=== 标签分配概况 ===\n\n";
        for (int tag = 1; tag <= tagCount; ++tag) {
            int totalAllocated = 0;
            for (int disk = 1; disk <= diskCount; ++disk) {
                totalAllocated += tagDiskAllocation[tag][disk];
            }
            
            outFile << "标签 " << tag << ":\n";
            outFile << "  目标分配单元: " << tagTotalUnits[tag] << "\n";
            outFile << "  实际分配单元: " << totalAllocated << " (" 
                    << std::fixed << std::setprecision(2)
                    << (tagTotalUnits[tag] > 0 ? (static_cast<double>(totalAllocated) / tagTotalUnits[tag] * 100) : 0)
                    << "%)\n";
            outFile << "  分配磁盘数: " << tagToDiskMap[tag].size() << "\n";
            outFile << "  分配磁盘详情: ";
            
            for (size_t i = 0; i < tagToDiskMap[tag].size(); ++i) {
                int disk = tagToDiskMap[tag][i];
                outFile << disk << "(" << tagDiskAllocation[tag][disk] << " 单元)";
                if (i < tagToDiskMap[tag].size() - 1) {
                    outFile << ", ";
                }
            }
            outFile << "\n\n";
        }
        
        outFile.close();
    }
    #endif
}

// 在文件末尾实现查询接口
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