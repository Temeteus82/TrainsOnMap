#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <cstring>

/// A QNetworkAccessManager whose replies never leave the process, so a test can
/// decide *when* each in-flight request finishes and in what order.
///
/// This exists because the two services that own the out-of-order-reply and
/// retry logic (TrainDetailsService, DigitrafficClient) issue every GET through
/// one manager, and DigitrafficClient issues three of them from its own
/// constructor — there is no seam short of handing them the manager. Both now
/// take one; here is the one the tests hand them.
namespace fake {

/// One canned reply. Inert until the test calls respond() or fail(), which emits
/// finished() synchronously — that synchronousness is the point: it is what lets
/// a test land reply B before reply A.
class Reply : public QNetworkReply
{
public:
    Reply(QNetworkAccessManager::Operation op, const QNetworkRequest &request,
          QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    /// Finish with 200 and `body`.
    void respond(const QByteArray &body)
    {
        m_body = body;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    /// Finish with a transport-level failure and no body.
    void fail(QNetworkReply::NetworkError code = QNetworkReply::HostNotFoundError)
    {
        setError(code, QStringLiteral("fake network failure"));
        setFinished(true);
        emit errorOccurred(code);
        emit finished();
    }

    void abort() override { fail(QNetworkReply::OperationCanceledError); }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        return QNetworkReply::bytesAvailable() + m_body.size() - m_read;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 left = m_body.size() - m_read;
        if (left <= 0)
            return -1;
        const qint64 n = qMin(left, maxSize);
        std::memcpy(data, m_body.constData() + m_read, n);
        m_read += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_read = 0;
};

/// Hands out fake::Reply for every request and records them in issue order.
/// Replies are QPointer-held because the code under test deleteLater()s them.
class Manager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

    QList<QPointer<Reply>> issued;

    /// The first still-live reply whose URL contains `needle`, removed from the
    /// list so a repeated fetch of the same endpoint is a distinct take().
    /// Returns nullptr when there is none — callers QVERIFY that.
    Reply *take(const QString &needle)
    {
        for (int i = 0; i < issued.size(); ++i) {
            Reply *r = issued.at(i);
            if (!r) {                       // already destroyed — drop the hole
                issued.removeAt(i--);
                continue;
            }
            if (r->url().toString().contains(needle)) {
                issued.removeAt(i);
                return r;
            }
        }
        return nullptr;
    }

    /// How many live requests are outstanding for `needle`.
    int pending(const QString &needle)
    {
        int n = 0;
        for (const QPointer<Reply> &r : issued)
            if (r && r->url().toString().contains(needle))
                ++n;
        return n;
    }

    void forget() { issued.clear(); }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *) override
    {
        auto *reply = new Reply(op, request, this);
        issued.push_back(reply);
        return reply;
    }
};

} // namespace fake
