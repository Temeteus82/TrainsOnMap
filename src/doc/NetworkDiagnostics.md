# NetworkDiagnostics.h (`netdiag` namespace)

## A. Overview

Header-only TLS diagnostics for the app's network clients. Four classes own a
`QNetworkAccessManager` — `DigitrafficClient`, `TrainDetailsService`,
`StationBoardService`, `FmiWeatherClient` — and none of them handled the
`sslErrors` signal. Without a handler a certificate problem reaches the user as
a generic "request failed" (or, on the MQTT socket, a bare "Socket error") with
the actual reason discarded, which makes it undiagnosable from a bug report.

This header supplies one function that wires a logging handler onto a manager.

## B. Security note — why this does *not* call `ignoreSslErrors()`

`QNetworkReply::ignoreSslErrors()` is the usual companion to an `sslErrors`
handler and is **deliberately absent** here. Every endpoint the app talks to is
public and credential-free, so a TLS failure is not a security event in itself —
but suppressing certificate validation would convert a missing-diagnostics gap
into a genuine vulnerability (a MITM could then feed the app arbitrary train
data). The handler logs and returns; the request fails exactly as it did before.

## C. Functions

#### void logSslErrors(QNetworkAccessManager \*net, const char \*context)

Connects a handler that logs, via `qWarning`, every certificate error reported
for any request the manager issues: the request URL and each error's
`errorString()`. `context` names the owning class, so a warning is attributable
when several clients have requests in flight at once.

The manager is used as the connection's context object, so the handler's
lifetime is tied to it and no manual disconnect is needed.

Compiled only when `QT_CONFIG(ssl)` is set. On an SSL-less Qt build the function
degrades to an empty inline — such a build cannot reach the app's HTTPS/WSS
endpoints at all, so there is nothing to diagnose, and the call sites still
compile.

## D. Scope — what lives here and what doesn't

`DigitrafficMqttClient`'s `QWebSocket` has the same gap but a different signal
shape (`sslErrors(const QList<QSslError> &)`, no reply argument). Its handler is
inlined at its single call site in `DigitrafficMqttClient.cpp` rather than added
here, so that including this header does not drag Qt WebSockets into the four
translation units that have nothing to do with WebSockets.

## E. Dependencies

`QNetworkAccessManager` unconditionally; `QDebug`, `QNetworkReply`, `QSslError`,
`QStringList` behind the `QT_CONFIG(ssl)` guard. Qt Network only.

Consumed by the four `QNetworkAccessManager` owners listed above.

## F. Usage Example

```cpp
#include "NetworkDiagnostics.h"

DigitrafficClient::DigitrafficClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_net->setTransferTimeout(kRequestTimeout);
    netdiag::logSslErrors(m_net, "DigitrafficClient");
}
```

A failing handshake then logs, e.g.:

```text
DigitrafficClient: TLS error for https://rata.digitraffic.fi/api/v1/…:
The certificate is self-signed, and untrusted
```

while the reply still completes with `QNetworkReply::SslHandshakeFailedError`.
