#pragma once

#include <string>

namespace sbc::server::gameserver {

struct Mail {
    std::string to;
    std::string subject;
    std::string body;
};

// Mailer abstracts outbound password-reset email. The local default logs instead
// of sending.
class Mailer {
public:
    virtual ~Mailer() = default;
    virtual void send(const Mail& mail) = 0;
};

class LogMailer : public Mailer {
public:
    void send(const Mail& mail) override;
};

}  // namespace sbc::server::gameserver
