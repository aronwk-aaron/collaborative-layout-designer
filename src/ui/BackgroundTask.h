#pragma once

#include <QApplication>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QWidget>

#include <functional>
#include <memory>

namespace cld::ui {

// Run a CPU-bound `work` callable on a background thread and show a
// modal busy progress dialog on the UI thread while it executes.
// Returns when the work finishes (or the caller closes the dialog,
// though we don't actually cancel — the work runs to completion).
//
// This is the minimal "keep the UI alive during a slow synchronous
// operation" pattern. The work callable must be self-contained — no
// touching of GUI objects from the worker thread.
//
// Usage:
//
//   ui::runBackground(parent, tr("Loading model..."), [&]{
//       result = expensiveBake();
//   });
//   // dialog has closed; result is ready.
inline void runBackground(QWidget* parent, const QString& busyText,
                           std::function<void()> work) {
    QProgressDialog dlg(busyText, QString(), 0, 0, parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setCancelButton(nullptr);          // no user cancel — work is non-interruptible
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);

    // Run the work on a one-shot QThread so the UI thread stays free
    // to pump paint events. Using QThread directly (rather than
    // QtConcurrent::run) keeps Qt6::Concurrent out of cld_ui's link
    // line for one helper.
    class WorkerThread : public QThread {
    public:
        std::function<void()> fn;
        void run() override { fn(); }
    };
    WorkerThread thread;
    thread.fn = std::move(work);

    QEventLoop loop;
    QObject::connect(&thread, &QThread::finished, &loop, &QEventLoop::quit);

    dlg.show();
    QApplication::processEvents();         // paint dialog before blocking
    thread.start();
    loop.exec();                           // pumps events while waiting

    dlg.close();
    thread.wait();                         // already finished, just sync
}

}  // namespace cld::ui
