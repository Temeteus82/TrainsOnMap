#pragma once

#include <QNetworkAccessManager>
#include <QtGlobal>

#if QT_CONFIG(ssl)
#include <QDebug>
#include <QList>
#include <QNetworkReply>
#include <QSslError>
#include <QStringList>
#endif

/// Shared network diagnostics. All four `QNetworkAccessManager` owners talk to
/// public, credential-free HTTPS endpoints, so a TLS failure is not a security
/// event here — but without a handler it surfaces only as a generic "request
/// failed", which is undiagnosable. These helpers log the certificate errors
/// and nothing else.
namespace netdiag {

#if QT_CONFIG(ssl)

/// Log TLS handshake errors for every request this manager issues.
///
/// Deliberately does **not** call `QNetworkReply::ignoreSslErrors()`: the
/// request still fails exactly as it does today. The only change is that the
/// reason ends up in the log instead of being discarded. `context` names the
/// owner so a warning is attributable when several clients are in flight.
///
/// The manager is its own connection context, so the handler dies with it.
inline void logSslErrors(QNetworkAccessManager *net, const char *context)
{
    QObject::connect(net, &QNetworkAccessManager::sslErrors, net,
                     [context](QNetworkReply *reply, const QList<QSslError> &errors) {
                         QStringList reasons;
                         reasons.reserve(errors.size());
                         for (const QSslError &e : errors)
                             reasons << e.errorString();
                         qWarning("%s: TLS error for %ls: %ls", context,
                                  qUtf16Printable(reply->request().url().toString()),
                                  qUtf16Printable(reasons.join(QStringLiteral("; "))));
                     });
}

#else

/// SSL-less Qt build: the app can't reach its HTTPS/WSS endpoints at all, so
/// there is nothing to diagnose. Keep the call sites compiling.
inline void logSslErrors(QNetworkAccessManager *, const char *) {}

#endif   // QT_CONFIG(ssl)

}   // namespace netdiag
