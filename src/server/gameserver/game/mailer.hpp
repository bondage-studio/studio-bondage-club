#pragma once

#include <string>

namespace sbc::server::gameserver {

struct Mail {
    std::string to;
    std::string subject;
    std::string body;
};

// Mailer abstracts outbound email. The original BC server uses nodemailer for
// password-reset codes; locally we default to a logging no-op (the reset code is
// surfaced via the log / admin API). A real SMTP implementation is a TODO behind
// this interface.
class Mailer {
public:
    virtual ~Mailer() = default;
    virtual void send(const Mail& mail) = 0;
};

// LogMailer logs the mail instead of sending it.
class LogMailer : public Mailer {
public:
    void send(const Mail& mail) override;
};

}  // namespace sbc::server::gameserver
