#pragma once
#include <string>

// Returns the TLS cert/key pair, generating and persisting a self-signed one on first boot.
bool ensureCert(std::string& certPem, std::string& keyPem);
// Replaces the stored cert with a new one whose Subject Alternative Name is the given IPv4 address, so it can be
// installed as trusted on a client. The new cert is used after the next restart.
bool regenerateCert(const std::string& ip);
// IPv4 address the stored cert was made for; empty if it has none (older builds).
std::string certIp();
