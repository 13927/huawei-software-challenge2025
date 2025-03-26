#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <vector>
#include <utility>
#include <cstddef>
#include <set>
#include <map>
#include <tuple>
// #include <functional>

// 前向声明
class FrequencyData;

// TODO: 
// 1. 磁盘分配算法可以修改
// 2. 未引入Tag
// 3. 可以分散存储在多个碎片块中

/**
 * 磁盘管理器类，用于模拟对磁盘的操作
 * 
 * 管理N个磁盘，每个磁盘包含V个存储单元
 * 存储单元状态:
 * -1: 表示该存储单元空闲
 * >=0: 表示存储单元被分配但未被读取
 * >=1: 表示该存储单元在object内的排序
 */
class DiskManager {
public:
    /**
     * 构造函数，初始化磁盘管理器
     * 参数 diskNum: 磁盘数量N
     * 参数 unitNum: 每个磁盘的存储单元数V
     * 参数 frequencyData: 频率数据对象，用于初始化预分配空间
     */
    DiskManager(int diskNum, int unitNum, FrequencyData& frequencyData);

    /**
     * 析构函数
     */
    ~DiskManager();

    /**
     * 分配指定磁盘上的存储单元
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 size: 需要分配的存储单元数量
     * 参数 tag: 标签ID，用于在预分配空间中分配
     * 返回值: 分配的存储单元块列表，每个pair表示<起始位置, 长度>
     *        如果找不到连续空间，会尝试碎片化存储在多个块中
     *        如果分配失败则返回空向量
     */
    std::vector<std::pair<int, int>> allocateOnDisk(int diskId, int size, int tag);

    /**
     * 分配指定磁盘上的存储单元（不指定标签）
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 size: 需要分配的存储单元数量
     * 返回值: 分配的存储单元块列表，每个pair表示<起始位置, 长度>
     *        如果找不到连续空间，会尝试碎片化存储在多个块中
     *        如果分配失败则返回空向量
     */
    std::vector<std::pair<int, int>> allocateOnDisk(int diskId, int size);

    /**
     * 释放指定磁盘上的存储单元
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 blocks: 存储单元块列表，每个pair表示<起始位置, 长度>
     * 返回值: 是否释放成功
     */
    bool freeOnDisk(int diskId, const std::vector<std::pair<int, int>>& blocks);

    /**
     * 查询指定磁盘的可用存储单元数量
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 返回值: 可用存储单元数量
     */
    int getFreeSpaceOnDisk(int diskId) const;

    /**
     * 获取总磁盘数量
     * 返回值: 磁盘数量N
     */
    int getDiskCount() const;

    /**
     * 获取每个磁盘的存储单元数量
     * 返回值: 存储单元数量V
     */
    int getUnitCount() const;

    /**
     * 检查指定磁盘上的特定单元是否为空闲状态
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 position: 存储单元位置 (1 <= position <= V)
     * 返回值: 如果该单元为空闲状态则返回true，否则返回false
     */
    bool isBlockFree(int diskId, int position) const;
    
    /**
     * 设置磁盘块在object内的排序
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 position: 存储单元位置 (1 <= position <= V)
     * 参数 objectIndex: object的index (>= 0)
     * 返回值: 是否设置成功
     */
    bool setBlockRead(int diskId, int position, int objectIndex);
    
    /**
     * 获取磁盘块的读取状态
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 position: 存储单元位置 (1 <= position <= V)
     * 返回值: 块的读取状态，-1表示空闲，>=0表示在object内的排序
     */
    int getBlockStatus(int diskId, int position) const;

    /**
     * 获取负载最小的N个磁盘ID
     * 参数 count: 要获取的磁盘数量
     * 返回值: 负载最小的N个磁盘ID列表
     */
    std::vector<int> getLeastLoadedDisks(int count) const;

    /**
     * 获取指定磁盘的负载情况（已分配单元数量）
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 返回值: 已分配单元数量
     */
    int getDiskLoad(int diskId) const;

    /**
     * 获取指定标签在指定磁盘上的空闲块数量
     * 参数 diskId: 磁盘ID (1 <= diskId <= N)
     * 参数 tag: 标签ID
     * 返回值: 空闲块数量，如果参数无效则返回-1
     */
    int getTagFreeSpace(int diskId, int tag) const;

private:
    int n;  // 磁盘数量
    int v;  // 每个磁盘的存储单元数量
    
    // 使用二维数组存储单元状态
    // diskUnits[i][j] 表示第i个磁盘的第j个单元的状态
    // -1: 空闲,  >= 0: 在object内的排序
    std::vector<std::vector<int>> diskUnits;

    FrequencyData& frequencyData;
    
    // 每个磁盘的空闲块数量
    std::vector<int> diskFreeSpaces;
    
    // 每个磁盘每个标签的预分配空闲块数量
    // diskTagFreeSpaces[diskId][tag] 表示第diskId个磁盘上标签tag的预分配空闲块数量
    std::vector<std::vector<int>> diskTagFreeSpaces;
    
    // 每个磁盘的标签区间映射
    // diskTagRanges[diskId] 存储该磁盘上的所有标签区间
    // 每个区间是一个元组 (startUnit, endUnit, tag)
    std::vector<std::vector<std::tuple<int, int, int>>> diskTagRanges;
    
    // 更新磁盘负载信息
    void updateDiskLoadInfo();
    
    // 查找连续空闲单元
    std::pair<int, int> findConsecutiveFreeUnits(int diskId, int size) const;
    
    // 在指定区间内查找连续空闲单元
    std::pair<int, int> findConsecutiveFreeUnits(int diskId, int size, int startUnit, int endUnit) const;
    
    // 初始化预分配空间
    void initializePreallocatedSpace();
    
    // 更新指定标签在指定磁盘上的空闲块数量
    void updateTagFreeSpace(int diskId, int tag, int change);
};

#endif // DISK_MANAGER_H 