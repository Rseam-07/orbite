#pragma once

#include "common.h"

#include <QFutureWatcher>
#include <QtConcurrent>
#include <optional>
#include <type_traits>

namespace orbite {

template <typename T>
struct JobResult {
    std::optional<T> value;
    QString error;
};

template <typename Work, typename Done>
void runJob(QObject *context, Work work, Done done)
{
    using T = std::invoke_result_t<Work>;
    auto *watcher = new QFutureWatcher<JobResult<T>>(context);
    QObject::connect(watcher, &QFutureWatcher<JobResult<T>>::finished, context,
                     [watcher, done = std::move(done)] {
        auto result = watcher->result();
        watcher->deleteLater();
        done(result);
    });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)]() -> JobResult<T> {
        try {
            return {work(), {}};
        } catch (const Failure &error) {
            return {{}, error.text};
        } catch (const std::exception &error) {
            return {{}, QString::fromUtf8(error.what())};
        }
    }));
}

}
