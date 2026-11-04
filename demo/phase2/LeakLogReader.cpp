// =============================================================================
// 文件：demo/phase2/LeakLogReader.cpp
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段二扩展 —— 泄漏日志查看
// 用途：实现日志发现与 v1 解析。解析是只读的，不触碰跟踪器状态。
// =============================================================================
#include "LeakLogReader.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

namespace {

// 统一设置错误文本；error 允许为空。
void setError(QString* error, const QString& message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

} // namespace

QStringList leaklog::listLogFiles(const QString& rootDir)
{
    QFileInfoList files;
    const QDir root(rootDir);
    if (!root.exists())
    {
        return QStringList();
    }

    // 递归覆盖 <root>/<phase>/<run>/*.log；调用方传入 phase 层或 logs 层都可。
    QDirIterator iterator(root.absolutePath(), QStringList() << QStringLiteral("*.log"),
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        iterator.next();
        files.append(iterator.fileInfo());
    }

    // 最近修改的日志排在前面，界面默认加载第一项。
    std::sort(files.begin(), files.end(),
              [](const QFileInfo& left, const QFileInfo& right)
              {
                  if (left.lastModified() != right.lastModified())
                  {
                      return left.lastModified() > right.lastModified();
                  }
                  return left.absoluteFilePath() > right.absoluteFilePath();
              });

    QStringList result;
    result.reserve(files.size());
    for (const QFileInfo& info : files)
    {
        result.append(info.absoluteFilePath());
    }
    return result;
}

bool leaklog::readLog(const QString& path, LeakLogData& out, QString* error)
{
    out = LeakLogData();
    if (error != nullptr)
    {
        error->clear();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setError(error, QStringLiteral("无法打开日志文件：%1").arg(path));
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QStringList lines = stream.readAll().split(QLatin1Char('\n'));

    // v1 block 行固定键序，file 作为行尾原始值（允许空格）。
    static const QRegularExpression blockPattern(QStringLiteral(
        "^block seq=(\\d+) addr=(0x[0-9A-Fa-f]+) size=(\\d+) "
        "tid=(\\d+) align=(\\d+) line=(-?\\d+) file=(.*)$"));

    bool headerSeen = false;
    bool endSeen = false;

    for (QString line : lines)
    {
        if (line.endsWith(QLatin1Char('\r')))
        {
            line.chop(1);
        }
        if (line.isEmpty())
        {
            continue;
        }

        if (!headerSeen)
        {
            if (line != QStringLiteral("# qtmemtrack leak report v1"))
            {
                setError(error, QStringLiteral("首行不是 v1 报告头：%1").arg(line));
                return false;
            }
            headerSeen = true;
            continue;
        }

        if (line == QStringLiteral("# end"))
        {
            endSeen = true;
            break;
        }

        const QRegularExpressionMatch match = blockPattern.match(line);
        if (match.hasMatch())
        {
            LeakBlock block;
            block.seq = match.captured(1).toULongLong();
            block.address = match.captured(2);
            block.size = match.captured(3).toULongLong();
            block.tid = match.captured(4).toUInt();
            block.align = match.captured(5).toUInt();
            block.line = match.captured(6).toInt();
            block.file = match.captured(7);
            out.blocks.append(block);
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals > 0)
        {
            out.header.insert(line.left(equals), line.mid(equals + 1));
        }
        // 其余无法识别的行忽略，保持对后续 v1 追加键的兼容。
    }

    if (!headerSeen)
    {
        setError(error, QStringLiteral("日志为空或缺少 v1 报告头"));
        return false;
    }
    if (!endSeen)
    {
        setError(error, QStringLiteral("日志不完整：缺少 # end"));
        return false;
    }
    return true;
}
