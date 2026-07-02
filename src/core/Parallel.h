#pragma once

#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <functional>
#include <numeric>
#include <vector>

/*
 * parallelRows(count, fn) — run fn(begin, end) over [0, count) split into
 * chunks across the global thread pool. The chunks MUST be independent
 * (no cross-row data flow). Safe to call from a pool thread: blockingMap
 * participates in the work instead of waiting, so nesting can't deadlock.
 * Small counts run inline to avoid scheduling overhead.
 */
inline void parallelRows(int count, const std::function<void(int, int)>& fn)
{
    const int threads = QThread::idealThreadCount();
    if (count < 128 || threads < 2) { fn(0, count); return; }

    const int nChunks = qMin(count, threads * 4);
    std::vector<int> idx(static_cast<size_t>(nChunks));
    std::iota(idx.begin(), idx.end(), 0);
    QtConcurrent::blockingMap(idx, [&](int c) {
        fn(int(qint64(count) * c / nChunks),
           int(qint64(count) * (c + 1) / nChunks));
    });
}
