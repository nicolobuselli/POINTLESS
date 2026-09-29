#pragma once
#include "Widgets.h"
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>
#include <atomic>
#include <type_traits>

// The dialog keeps document editing modal while the immutable job runs off-thread.
// Cancellation is cooperative; a renderer already inside a frame finishes that frame.
template<class Task>
auto runBackgroundJob(QWidget* parent, const QString& label, int maximum, Task task,
                      int delayMs = 150)
{
    using Result = std::invoke_result_t<Task, std::atomic_bool&, std::atomic_int&>;
    std::atomic_bool cancel{false};
    std::atomic_int value{0};
    AnimProgressDialog dialog(label, maximum, parent);
    QElapsedTimer elapsed; elapsed.start();
    if (delayMs > 0) dialog.setWindowOpacity(0.0);
    dialog.setMinimumDuration(0);
    dialog.show();
    QEventLoop loop;
    QFutureWatcher<Result> watcher;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (elapsed.elapsed() >= delayMs || dialog.wasCanceled()) dialog.setWindowOpacity(1.0);
        if (dialog.wasCanceled()) {
            cancel.store(true);
            dialog.setLabelText("Canceling after the current operation…");
        }
        dialog.setValue(value.load());
    });
    QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([&] { return task(cancel, value); }));
    timer.start(30);
    if (!watcher.isFinished()) loop.exec();
    timer.stop();
    watcher.waitForFinished();
    dialog.accept();
    return watcher.result();
}
