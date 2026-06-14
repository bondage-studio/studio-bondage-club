#include "server/gameserver/game/mailer.hpp"

#include <spdlog/spdlog.h>

namespace sbc::server::gameserver {

void LogMailer::send(const Mail& mail) {
    spdlog::info("gameserver: [mail] to={} subject=\"{}\" body=\"{}\"", mail.to, mail.subject,
                 mail.body);
}

}  // namespace sbc::server::gameserver
