#ifndef CONSTANTS_H
#define CONSTANTS_H

// 系统常量
#define MAX_DISK_NUM (10 + 1)
#define MAX_DISK_SIZE (16384 + 1)
#define MAX_REQUEST_NUM (30000000 + 1)
#define MAX_OBJECT_NUM (100000 + 1)
#define REP_NUM 3
#define FRE_PER_SLICING (1800)
#define EXTRA_TIME (105)

extern int currentTimeSlice;

#endif // CONSTANTS_H 