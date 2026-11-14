#include "proxy_server.h"
#include "modem_manager.h"

#include <mbedtls/base64.h>

void ProxyServer::begin(ConfigManager* cfg, ModemManager* modem) {
    _cfg = cfg;
    _modem = modem;
    for (int i = 0; i < MAX_PROXY_CLIENTS; ++i) {
        _slots[i].linkId = i; // Uses A7670E link IDs 0..3
    }
    setEnabled(_cfg->get().proxyEnabled);
}

void ProxyServer::setEnabled(bool enabled) {
    if (enabled == _enabled && _httpServer != nullptr) return;
    _enabled = enabled;
    if (!enabled) {
        for (int i = 0; i < MAX_PROXY_CLIENTS; ++i) {
            closeSlot(i);
        }
        if (_httpServer) { _httpServer->stop(); delete _httpServer; _httpServer = nullptr; }
        if (_socksServer) { _socksServer->stop(); delete _socksServer; _socksServer = nullptr; }
        Serial.println("[Proxy] Disabled");
    } else {
        restartListeners();
    }
}

void ProxyServer::restartListeners() {
    if (_httpServer) { _httpServer->stop(); delete _httpServer; _httpServer = nullptr; }
    if (_socksServer) { _socksServer->stop(); delete _socksServer; _socksServer = nullptr; }

    _httpPort = _cfg->get().httpProxyPort;
    _socksPort = _cfg->get().socksProxyPort;

    _httpServer = new WiFiServer(_httpPort);
    _httpServer->begin();
    _httpServer->setNoDelay(true);

    _socksServer = new WiFiServer(_socksPort);
    _socksServer->begin();
    _socksServer->setNoDelay(true);

    Serial.printf("[Proxy] Listening HTTP:%u SOCKS5:%u\n", _httpPort, _socksPort);
}

bool ProxyServer::requiresAuth() const {
    const auto& c = _cfg->get();
    return c.proxyUser.length() > 0 || c.proxyPass.length() > 0;
}

bool ProxyServer::checkHttpProxyAuth(const String& headers) const {
    if (!requiresAuth()) return true;
    int idx = headers.indexOf("Proxy-Authorization: Basic ");
    if (idx < 0) idx = headers.indexOf("proxy-authorization: Basic ");
    if (idx < 0) return false;

    int start = idx + 27;
    int end = headers.indexOf('\r', start);
    if (end < 0) end = headers.indexOf('\n', start);
    String b64 = (end > start) ? headers.substring(start, end) : headers.substring(start);
    b64.trim();

    String expectedRaw = _cfg->get().proxyUser + ":" + _cfg->get().proxyPass;
    unsigned char encoded[160];
    size_t outLen = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &outLen,
                          reinterpret_cast<const unsigned char*>(expectedRaw.c_str()),
                          expectedRaw.length());
    encoded[outLen] = '\0';
    return b64 == String(reinterpret_cast<char*>(encoded));
}

bool ProxyServer::parseHostPort(const String& hostPort, uint16_t defaultPort, String& hostOut, uint16_t& portOut) {
    int colon = hostPort.lastIndexOf(':');
    if (colon > 0) {
        hostOut = hostPort.substring(0, colon);
        portOut = static_cast<uint16_t>(hostPort.substring(colon + 1).toInt());
        if (portOut == 0) portOut = defaultPort;
    } else {
        hostOut = hostPort;
        portOut = defaultPort;
    }
    hostOut.trim();
    return hostOut.length() > 0;
}

void ProxyServer::acceptNewClients() {
    auto assignClient = [this](WiFiClient& cli, bool isSocks) {
        for (int i = 0; i < MAX_PROXY_CLIENTS; ++i) {
            if (_slots[i].state == TunnelState::Free) {
                _slots[i].client = cli;
                _slots[i].client.setNoDelay(true);
                _slots[i].state = isSocks ? TunnelState::SocksGreeting : TunnelState::HttpHandshake;
                _slots[i].rxBuf = "";
                _slots[i].isSocks = isSocks;
                _slots[i].lastActivityMs = millis();
                _slots[i].lastPollMs = 0;
                _totalConnections++;
                return true;
            }
        }
        return false;
    };

    if (_httpServer) {
        WiFiClient c = _httpServer->available();
        if (c) {
            if (!assignClient(c, false)) {
                c.print("HTTP/1.1 503 Service Unavailable\r\n\r\n");
                c.stop();
            }
        }
    }
    if (_socksServer) {
        WiFiClient c = _socksServer->available();
        if (c) {
            if (!assignClient(c, true)) {
                c.stop();
            }
        }
    }
}

void ProxyServer::closeSlot(int idx) {
    TunnelSlot& s = _slots[idx];
    if (s.state == TunnelState::Free) return;
    if (s.state == TunnelState::Relaying && _modem) {
        _modem->tcpClose(s.linkId);
    }
    if (s.client.connected()) {
        s.client.stop();
    }
    s.state = TunnelState::Free;
    s.rxBuf = "";
}

void ProxyServer::processSlot(int idx) {
    TunnelSlot& s = _slots[idx];
    if (s.state == TunnelState::Free) return;

    if (!s.client.connected() && !s.client.available()) {
        closeSlot(idx);
        return;
    }

    // Idle timeout (90s for active tunnels, 10s for handshake)
    uint32_t timeout = (s.state == TunnelState::Relaying) ? 90000UL : 10000UL;
    if (millis() - s.lastActivityMs > timeout) {
        closeSlot(idx);
        return;
    }

    if (s.state == TunnelState::HttpHandshake) {
        while (s.client.available() && s.rxBuf.length() < 4096) {
            s.rxBuf += static_cast<char>(s.client.read());
            s.lastActivityMs = millis();
            if (s.rxBuf.endsWith("\r\n\r\n")) break;
        }
        if (!s.rxBuf.endsWith("\r\n\r\n")) return;

        if (!checkHttpProxyAuth(s.rxBuf)) {
            s.client.print("HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm=\"A7670E\"\r\n\r\n");
            closeSlot(idx);
            return;
        }

        int lineEnd = s.rxBuf.indexOf("\r\n");
        String reqLine = s.rxBuf.substring(0, lineEnd);
        int sp1 = reqLine.indexOf(' ');
        int sp2 = (sp1 > 0) ? reqLine.indexOf(' ', sp1 + 1) : -1;
        if (sp1 < 0 || sp2 <= sp1) {
            s.client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
            closeSlot(idx);
            return;
        }

        String method = reqLine.substring(0, sp1);
        String target = reqLine.substring(sp1 + 1, sp2);
        String version = reqLine.substring(sp2 + 1);

        String host;
        uint16_t port = 80;
        String err;

        if (method == "CONNECT") {
            if (!parseHostPort(target, 443, host, port)) {
                s.client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
                closeSlot(idx);
                return;
            }
            if (!_modem->tcpOpen(s.linkId, host, port, err)) {
                s.client.print("HTTP/1.1 502 Bad Gateway\r\n\r\n" + err);
                closeSlot(idx);
                return;
            }
            s.client.print("HTTP/1.1 200 Connection Established\r\n\r\n");
            s.rxBuf = "";
            s.state = TunnelState::Relaying;
            s.lastActivityMs = millis();
            return;
        } else {
            // Plain HTTP proxy: e.g. GET http://example.com:80/path HTTP/1.1
            String url = target;
            if (url.startsWith("http://")) url = url.substring(7);
            int slash = url.indexOf('/');
            String hostPort = (slash >= 0) ? url.substring(0, slash) : url;
            String path = (slash >= 0) ? url.substring(slash) : "/";
            if (!parseHostPort(hostPort, 80, host, port)) {
                s.client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
                closeSlot(idx);
                return;
            }
            if (!_modem->tcpOpen(s.linkId, host, port, err)) {
                s.client.print("HTTP/1.1 502 Bad Gateway\r\n\r\n" + err);
                closeSlot(idx);
                return;
            }

            // Rewrite request without absolute URL & Proxy-* headers
            String rebuilt = method + " " + path + " " + version + "\r\n";
            int pos = lineEnd + 2;
            while (pos < static_cast<int>(s.rxBuf.length())) {
                int next = s.rxBuf.indexOf("\r\n", pos);
                if (next <= pos) break;
                String hdr = s.rxBuf.substring(pos, next);
                if (!hdr.startsWith("Proxy-Connection:") && !hdr.startsWith("Proxy-Authorization:")) {
                    rebuilt += hdr + "\r\n";
                }
                pos = next + 2;
            }
            rebuilt += "Connection: close\r\n\r\n";

            _modem->tcpSend(s.linkId, reinterpret_cast<const uint8_t*>(rebuilt.c_str()), rebuilt.length());
            _bytesUp += rebuilt.length();
            s.rxBuf = "";
            s.state = TunnelState::Relaying;
            s.lastActivityMs = millis();
            return;
        }
    }

    if (s.state == TunnelState::SocksGreeting) {
        if (s.client.available() < 2) return;
        uint8_t ver = s.client.read();
        uint8_t nmethods = s.client.read();
        if (ver != 0x05) { closeSlot(idx); return; }

        uint8_t methods[255];
        uint32_t waitStart = millis();
        int got = 0;
        while (got < nmethods && millis() - waitStart < 1000) {
            if (s.client.available()) methods[got++] = s.client.read();
        }
        bool needAuth = requiresAuth();
        bool found = false;
        uint8_t chosen = needAuth ? 0x02 : 0x00;
        for (int i = 0; i < got; ++i) {
            if (methods[i] == chosen) { found = true; break; }
        }
        if (!found) {
            uint8_t rej[2] = {0x05, 0xFF};
            s.client.write(rej, 2);
            closeSlot(idx);
            return;
        }
        uint8_t reply[2] = {0x05, chosen};
        s.client.write(reply, 2);
        s.state = needAuth ? TunnelState::SocksAuth : TunnelState::SocksRequest;
        s.lastActivityMs = millis();
        return;
    }

    if (s.state == TunnelState::SocksAuth) {
        if (s.client.available() < 2) return;
        uint8_t ver = s.client.read();
        uint8_t ulen = s.client.read();
        if (ver != 0x01) { closeSlot(idx); return; }
        char ubuf[128] = {0}, pbuf[128] = {0};
        uint8_t actualU = std::min<uint8_t>(ulen, 127);
        s.client.readBytes(ubuf, actualU);
        uint8_t plen = 0;
        if (s.client.readBytes(&plen, 1) != 1) { closeSlot(idx); return; }
        uint8_t actualP = std::min<uint8_t>(plen, 127);
        s.client.readBytes(pbuf, actualP);

        bool ok = (_cfg->get().proxyUser == String(ubuf)) && (_cfg->get().proxyPass == String(pbuf));
        uint8_t resp[2] = {0x01, static_cast<uint8_t>(ok ? 0x00 : 0x01)};
        s.client.write(resp, 2);
        if (!ok) { closeSlot(idx); return; }
        s.state = TunnelState::SocksRequest;
        s.lastActivityMs = millis();
        return;
    }

    if (s.state == TunnelState::SocksRequest) {
        if (s.client.available() < 4) return;
        uint8_t hdr[4];
        s.client.readBytes(hdr, 4);
        if (hdr[0] != 0x05 || hdr[1] != 0x01) {
            uint8_t errResp[10] = {0x05, 0x07, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
            s.client.write(errResp, 10);
            closeSlot(idx);
            return;
        }

        String host;
        uint8_t atyp = hdr[3];
        if (atyp == 0x01) {
            uint8_t ip[4];
            if (s.client.readBytes(ip, 4) != 4) { closeSlot(idx); return; }
            host = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        } else if (atyp == 0x03) {
            uint8_t dlen = 0;
            if (s.client.readBytes(&dlen, 1) != 1 || dlen == 0) { closeSlot(idx); return; }
            char dbuf[256] = {0};
            if (s.client.readBytes(dbuf, dlen) != dlen) { closeSlot(idx); return; }
            host = String(dbuf);
        } else {
            uint8_t errResp[10] = {0x05, 0x08, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
            s.client.write(errResp, 10);
            closeSlot(idx);
            return;
        }

        uint8_t portBytes[2];
        if (s.client.readBytes(portBytes, 2) != 2) { closeSlot(idx); return; }
        uint16_t port = (static_cast<uint16_t>(portBytes[0]) << 8) | portBytes[1];

        String err;
        if (!_modem->tcpOpen(s.linkId, host, port, err)) {
            uint8_t errResp[10] = {0x05, 0x05, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
            s.client.write(errResp, 10);
            closeSlot(idx);
            return;
        }

        uint8_t okResp[10] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        s.client.write(okResp, 10);
        s.state = TunnelState::Relaying;
        s.lastActivityMs = millis();
        return;
    }

    if (s.state == TunnelState::Relaying) {
        uint8_t buf[1024];

        // 1. Client -> Modem
        int avail = s.client.available();
        if (avail > 0) {
            int toRead = std::min<int>(avail, sizeof(buf));
            int n = s.client.read(buf, toRead);
            if (n > 0) {
                if (_modem->tcpSend(s.linkId, buf, static_cast<size_t>(n))) {
                    _bytesUp += n;
                    s.lastActivityMs = millis();
                } else {
                    closeSlot(idx);
                    return;
                }
            }
        }

        // 2. Modem -> Client
        if (_modem->hasLinkDataHint(s.linkId) ||
            _modem->isLinkRemoteClosed(s.linkId) ||
            (millis() - s.lastPollMs > 120)) {
            s.lastPollMs = millis();
            bool remoteClosed = false;
            int n = _modem->tcpRecv(s.linkId, buf, sizeof(buf), remoteClosed);
            if (n > 0) {
                s.client.write(buf, static_cast<size_t>(n));
                _bytesDown += n;
                s.lastActivityMs = millis();
            } else if (remoteClosed) {
                closeSlot(idx);
                return;
            }
        }
    }
}

void ProxyServer::loop() {
    if (!_enabled || !_modem) return;
    acceptNewClients();
    for (int i = 0; i < MAX_PROXY_CLIENTS; ++i) {
        processSlot(i);
    }
}

ProxyStats ProxyServer::getStats() const {
    ProxyStats st;
    st.enabled = _enabled;
    st.httpPort = _httpPort;
    st.socksPort = _socksPort;
    st.totalConnections = _totalConnections;
    st.bytesUp = _bytesUp;
    st.bytesDown = _bytesDown;
    for (int i = 0; i < MAX_PROXY_CLIENTS; ++i) {
        if (_slots[i].state != TunnelState::Free) st.activeTunnels++;
    }
    return st;
}
