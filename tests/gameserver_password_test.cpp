#include <string>

#include "common/base64.hpp"
#include "server/gameserver/game/password.hpp"
#include "test_framework.hpp"

using namespace sbc;
using namespace sbc::server::gameserver;

SBC_TEST(base64_round_trip) {
    for (const std::string& s : {std::string(""), std::string("f"), std::string("fo"),
                                 std::string("foo"), std::string("foob"), std::string("fooba"),
                                 std::string("foobar"), std::string("\x00\x01\x02\xff", 4)}) {
        CHECK(base64_decode(base64_encode(s)) == s);
    }
    CHECK(base64_encode("foobar") == "Zm9vYmFy");
}

SBC_TEST(password_hash_verify_round_trip) {
    std::string stored = hash_password("SECRET1");
    CHECK(stored.rfind("pbkdf2$", 0) == 0);
    CHECK(verify_password("SECRET1", stored));
    CHECK(!verify_password("secret1", stored));
    CHECK(!verify_password("WRONGPW", stored));
}

SBC_TEST(password_hashes_are_salted) {
    std::string a = hash_password("HELLO");
    std::string b = hash_password("HELLO");
    CHECK(a != b);
    CHECK(verify_password("HELLO", a));
    CHECK(verify_password("HELLO", b));
}

SBC_TEST(password_verify_rejects_malformed) {
    CHECK(!verify_password("x", ""));
    CHECK(!verify_password("x", "notpbkdf2"));
    CHECK(!verify_password("x", "pbkdf2$abc$zz$zz"));
    CHECK(!verify_password("x", "pbkdf2$1000$$"));
}
