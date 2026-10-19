#pragma once

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>

// Follow the process tree: runner executable names vary between Ollama versions.
inline qint64 ollamaResidentMemory(const QString &procRoot = QStringLiteral("/proc")) {
    struct Process { qint64 parent = 0; qint64 rss = -1; };
    QHash<qint64, Process> processes;
    QSet<qint64> selected;
    for (const auto &entry : QDir(procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        const auto pid = entry.toLongLong(&numeric);
        if (!numeric || pid <= 0) continue;
        const auto base = procRoot + '/' + entry;
        QFile cmd(base + "/cmdline");
        if (cmd.open(QIODevice::ReadOnly)) {
            const auto executable = QString::fromLocal8Bit(cmd.readAll().split('\0').value(0)).section('/', -1);
            if (executable == "ollama" || executable == "ollama_llama_server") selected.insert(pid);
        }
        QFile status(base + "/status");
        if (!status.open(QIODevice::ReadOnly)) continue;
        Process process;
        for (const auto &line : status.readAll().split('\n')) {
            const auto fields = line.simplified().split(' ');
            bool ok = false;
            const auto value = fields.value(1).toLongLong(&ok);
            if (!ok || value < 0) continue;
            if (fields.value(0) == "PPid:") process.parent = value;
            if (fields.value(0) == "VmRSS:") process.rss = value * 1024;
        }
        processes.insert(pid, process);
    }
    bool added;
    do {
        added = false;
        for (auto it = processes.cbegin(); it != processes.cend(); ++it) {
            if (!selected.contains(it.key()) && selected.contains(it->parent)) {
                selected.insert(it.key());
                added = true;
            }
        }
    } while (added);
    qint64 total = 0;
    bool found = false;
    for (const auto pid : selected) {
        const auto it = processes.constFind(pid);
        if (it != processes.cend() && it->rss >= 0) {
            total += it->rss;
            found = true;
        }
    }
    return found ? total : -1;
}
