// =============================================================================
// 文件：demo/phase2/LeakLogReader.h
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段二扩展 —— 泄漏日志查看
// 用途：发现并解析阶段一生成的 v1 泄漏日志（只读），供界面表格展示。
// 约束：
//   - 只支持 v1 格式；不修改日志、不依赖跟踪器内部数据结构。
//   - 仅在 GUI 线程调用；一次性读入内存，适用于开发期日志规模。
// =============================================================================
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

// 单条泄漏块记录，字段顺序与日志 v1 的 block 行一致。
struct LeakBlock
{
    quint64 seq = 0;        // 全局分配序号
    QString address;        // 地址原样字符串（0x%016llX）
    quint64 size = 0;       // 请求字节数
    quint32 tid = 0;        // 分配线程 ID
    quint32 align = 0;      // 0 = 普通分配；否则为对齐值
    int line = 0;           // 分配行号；未知为 0
    QString file;           // 分配文件；未知为 <unknown>
};

// 一份日志的解析结果。
struct LeakLogData
{
    QHash<QString, QString> header; // 头部 key=value
    QVector<LeakBlock> blocks;      // 按文件中的原始顺序保存
};

namespace leaklog {

// 递归扫描 rootDir/<phase>/<run>/*.log，按修改时间倒序返回绝对路径。
QStringList listLogFiles(const QString& rootDir);

// 解析 v1 日志。
// 成功返回 true，out 被覆盖；失败返回 false，并通过 error（可空）给出原因。
bool readLog(const QString& path, LeakLogData& out, QString* error = nullptr);

} // namespace leaklog
