#include "frequency_data.h"
#include <algorithm>
#include <numeric>
#include <cmath>

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
    
    // 每个磁盘分配的标签和对应的单元数
    std::vector<std::vector<std::pair<int, int>>> diskAllocation(diskCount + 1);
    
    // 每个标签分配到的磁盘列表
    std::vector<std::vector<int>> tagToDiskMap(tagCount + 1);
    
    // 按照峰值存储需求从大到小排序标签
    std::vector<std::pair<int, int>> sortedTagsByStorage;
    for (int tag = 1; tag <= tagCount; tag++) {
        sortedTagsByStorage.push_back({tag, peakStorageNeeds[tag]});
    }
    // 根据峰值存储需求排序
    std::sort(sortedTagsByStorage.begin(), sortedTagsByStorage.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
              
    // 提取排序后的标签ID列表
    std::vector<int> sortedTags;
    for (const auto& pair : sortedTagsByStorage) {
        sortedTags.push_back(pair.first);
    }
    
    // 为每个标签确定分配的磁盘数量 - 修改为只分配到3个磁盘
    std::vector<int> tagDiskCount(tagCount + 1, 0);
    
    // 将每个标签的磁盘分配数量设置为3，而不是diskCount
    const int DISKS_PER_TAG = 3; // 每个标签分配的磁盘数量
    for (int tag = 1; tag <= tagCount; tag++) {
        tagDiskCount[tag] = std::min(DISKS_PER_TAG, diskCount); 
    }
    
    #ifndef NDEBUG
    std::ofstream diskDebugFile("disk_allocation_debug.txt");
    if (diskDebugFile.is_open()) {
        diskDebugFile << "=== 磁盘分配调试信息 ===\n\n";
        diskDebugFile << "标签数量: " << tagCount << "\n";
        diskDebugFile << "磁盘数量: " << diskCount << "\n";
        diskDebugFile << "每个磁盘单元数: " << unitsPerDisk << "\n\n";
        
        diskDebugFile << "标签存储需求排序:\n";
        for (const auto& [tag, storage] : sortedTagsByStorage) {
            diskDebugFile << "标签 " << tag << ": 存储需求=" << storage << "\n";
        }
        diskDebugFile << "\n";
    }
    #endif
    
    // 每个标签每个磁盘分配的单元数
    std::vector<std::vector<int>> tagDiskAllocation(tagCount + 1, std::vector<int>(diskCount + 1, 0));
    
    // 计算总系统容量，使用100%
    int totalSystemUnits = diskCount * unitsPerDisk;
    int totalUnitsToAllocate = totalSystemUnits; // 使用全部容量
    
    // 调整每个标签的分配量，使总和为系统总容量
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
    
    // 第一步：根据相关性为每个标签选择最适合的磁盘
    for (int tag : sortedTags) {
        if (tagUnitsToBePlaced[tag] <= 0 || tagDiskCount[tag] <= 0) continue;
        
        // 计算每个磁盘应该分配的单元数（均匀分配）
        int targetDisksCount = tagDiskCount[tag];
        int unitsPerTargetDisk = tagUnitsToBePlaced[tag] / targetDisksCount;
        int remainingUnits = tagUnitsToBePlaced[tag] % targetDisksCount;
        
        // 计算每个磁盘与已分配标签的相关性得分
        std::vector<std::pair<int, double>> diskCorrelationScore;
        for (int disk = 1; disk <= diskCount; ++disk) {
            double correlationScore = 0.0;
            int totalAllocatedUnits = 0;
            
            // 计算该磁盘上已有标签与当前标签的相关性之和，加权计算
            for (int otherTag = 1; otherTag <= tagCount; ++otherTag) {
                if (otherTag == tag) continue;
                
                if (tagDiskAllocation[otherTag][disk] > 0) {
                    // 使用相关性乘以已分配单元数作为权重
                    correlationScore += getTagCorrelation(tag, otherTag) * tagDiskAllocation[otherTag][disk];
                    totalAllocatedUnits += tagDiskAllocation[otherTag][disk];
                }
            }
            
            // 计算可用空间
            int availableSpace = unitsPerDisk - diskAllocated[disk];
            
            // 评分公式：
            // 1. 负相关性分数（相关性越低越好）
            // 2. 可用空间比例（可用空间越大越好）
            // 3. 如果磁盘未分配任何标签，给予额外奖励分数
            double finalScore = -correlationScore; // 负相关性，相关性越低得分越高
            finalScore += (static_cast<double>(availableSpace) / unitsPerDisk) * 2.0; // 可用空间因子
            
            if (totalAllocatedUnits == 0) {
                finalScore += 1.0; // 空磁盘奖励
            }
            
            diskCorrelationScore.push_back({disk, finalScore});
        }
        
        // 按评分从高到低排序
        std::sort(diskCorrelationScore.begin(), diskCorrelationScore.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // 从评分最高的磁盘中选择targetDisksCount个
        std::vector<int> bestDisks;
        for (size_t i = 0; i < diskCorrelationScore.size() && bestDisks.size() < targetDisksCount; ++i) {
            int disk = diskCorrelationScore[i].first;
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
            
            // 输出每个磁盘的相关性评分
            diskDebugFile << "  磁盘评分情况: ";
            for (const auto& [disk, score] : diskCorrelationScore) {
                diskDebugFile << disk << "(" << score << ") ";
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
            
            // 如果当前标签分配的磁盘数量已达目标，则跳过未分配的磁盘
            if (!alreadyAllocated && tagToDiskMap[tag].size() >= tagDiskCount[tag]) {
                continue;
            }
            
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
            // 使用排序后的标签相关性来构建标签之间的相似度矩阵
            std::vector<std::vector<double>> similarityMatrix(tagsOnDisk.size(), 
                                                           std::vector<double>(tagsOnDisk.size(), 0.0));
            
            for (size_t i = 0; i < tagsOnDisk.size(); ++i) {
                for (size_t j = i+1; j < tagsOnDisk.size(); ++j) {
                    int tag1 = tagsOnDisk[i];
                    int tag2 = tagsOnDisk[j];
                    // 直接从tagCorrelation获取相关性
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
    
    // 确保所有磁盘空间都被充分利用
    int totalAllocatedSpace = 0;
    for (int disk = 1; disk <= diskCount; ++disk) {
        totalAllocatedSpace += diskAllocated[disk];
    }
    
    int remainingSpace = totalSystemUnits - totalAllocatedSpace;
    if (remainingSpace > 0) {
        #ifndef NDEBUG
        std::cerr << "系统还有 " << remainingSpace << " 单元未分配，将进行额外分配" << std::endl;
        #endif
        
        // 将剩余空间分配给各个标签，优先分配给存储需求高的标签
        for (const auto& [tag, storage] : sortedTagsByStorage) {
            if (remainingSpace <= 0) break;
            
            // 找出该标签已分配的磁盘
            std::vector<int> usedDisks = tagToDiskMap[tag];
            
            // 找出所有有剩余空间的磁盘
            std::vector<std::pair<int, int>> availableDisks; // <disk_id, available>
            for (int disk = 1; disk <= diskCount; ++disk) {
                int available = unitsPerDisk - diskAllocated[disk];
                if (available > 0) {
                    availableDisks.push_back({disk, available});
                }
            }
            
            if (!availableDisks.empty()) {
                // 按可用空间降序排序
                std::sort(availableDisks.begin(), availableDisks.end(),
                        [](const auto& a, const auto& b) { return a.second > b.second; });
                
                // 先尝试分配给已使用的磁盘
                for (int disk : usedDisks) {
                    if (remainingSpace <= 0) break;
                    
                    int available = unitsPerDisk - diskAllocated[disk];
                    if (available > 0) {
                        int toAllocate = std::min(remainingSpace, available);
                        tagDiskAllocation[tag][disk] += toAllocate;
                        diskAllocated[disk] += toAllocate;
                        remainingSpace -= toAllocate;
                        
                        // 更新磁盘分配结果
                        bool found = false;
                        for (auto& range : diskAllocationResult[disk]) {
                            if (range.tag == tag) {
                                range.endUnit += toAllocate;
                                found = true;
                                break;
                            }
                        }
                        
                        if (!found) {
                            // 创建新的分配范围
                            int startUnit = 1;
                            for (const auto& range : diskAllocationResult[disk]) {
                                startUnit = std::max(startUnit, range.endUnit + 1);
                            }
                            DiskRange range = {startUnit, startUnit + toAllocate - 1, tag};
                            diskAllocationResult[disk].push_back(range);
                            tagAllocationResult[tag].push_back(std::make_tuple(disk, startUnit, startUnit + toAllocate - 1));
                        }
                    }
                }
                
                // 如果还有剩余空间，分配给其他磁盘
                if (remainingSpace > 0 && tagToDiskMap[tag].size() < diskCount) {
                    for (const auto& [disk, available] : availableDisks) {
                        if (remainingSpace <= 0) break;
                        
                        // 检查是否已分配给该磁盘
                        if (std::find(usedDisks.begin(), usedDisks.end(), disk) != usedDisks.end()) {
                            continue; // 已分配过，跳过
                        }
                        
                        int toAllocate = std::min(remainingSpace, available);
                        tagDiskAllocation[tag][disk] += toAllocate;
                        diskAllocated[disk] += toAllocate;
                        tagToDiskMap[tag].push_back(disk);
                        remainingSpace -= toAllocate;
                        
                        // 创建新的分配范围
                        int startUnit = 1;
                        for (const auto& range : diskAllocationResult[disk]) {
                            startUnit = std::max(startUnit, range.endUnit + 1);
                        }
                        DiskRange range = {startUnit, startUnit + toAllocate - 1, tag};
                        diskAllocationResult[disk].push_back(range);
                        tagAllocationResult[tag].push_back(std::make_tuple(disk, startUnit, startUnit + toAllocate - 1));
                    }
                }
            }
        }
    }
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