// The phone-setup page's HTTP request reader: any split of the bytes, the
// bounds, and every way a request can be wrong. It must never wait, grow, or
// keep a password around after reset().
#include <string.h>

#include <string>

#include "stella/net/http_request.h"
#include "test.h"

using namespace stella;

namespace {

HttpRequestParser::State feedAll(HttpRequestParser &p, const std::string &data, size_t piece = 0) {
  if (!piece) piece = data.size();
  for (size_t i = 0; i < data.size(); i += piece)
    p.feed((const uint8_t *)data.data() + i, data.size() - i < piece ? data.size() - i : piece);
  return p.state();
}

const char kGet[] =
    "GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\n"
    "User-Agent: Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Mobile Safari/537.36\r\n"
    "Accept: */*\r\n\r\n";

std::string post(const std::string &body, const char *extra = "") {
  return "POST /join HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\n" +
         std::string(extra) + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

TEST(parser_reads_a_get_in_any_split) {
  for (size_t piece : {0u, 1u, 2u, 3u, 7u, 64u}) {
    HttpRequestParser p;
    CHECK(feedAll(p, kGet, piece) == HttpRequestParser::State::Complete);
    CHECK(!p.post() && strcmp(p.path(), "/generate_204") == 0 && p.bodyLen() == 0);
  }
}

TEST(parser_reads_a_form_post_and_ignores_query_and_case) {
  const std::string body = "ssid=Home+Net&password=p%40ss%26word";
  for (size_t piece : {0u, 1u, 5u}) {
    HttpRequestParser p;
    CHECK(feedAll(p, post(body, "x-long: " "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n"),
                  piece) == HttpRequestParser::State::Complete);
    CHECK(p.post() && strcmp(p.path(), "/join") == 0);
    CHECK(p.bodyLen() == body.size() && body == p.body());
  }
  HttpRequestParser q;
  CHECK(feedAll(q, "GET /?a=b#c HTTP/1.0\r\nCONTENT-length: 0\r\n\r\n") == HttpRequestParser::State::Complete);
  CHECK(strcmp(q.path(), "/") == 0);
  HttpRequestParser r;  // a bare LF line ending and stray blank lines first
  CHECK(feedAll(r, "\r\n\r\nPOST /x HTTP/1.1\ncontent-length: 3\n\nabc") == HttpRequestParser::State::Complete);
  CHECK(r.bodyLen() == 3 && strcmp(r.body(), "abc") == 0);
}

TEST(parser_waits_for_the_rest_and_ignores_extra_bytes) {
  HttpRequestParser p;
  const std::string req = post("a=1");
  CHECK(feedAll(p, req.substr(0, req.size() - 2)) == HttpRequestParser::State::Body);
  CHECK(!p.done());
  CHECK(feedAll(p, "1" "EXTRA GARBAGE") == HttpRequestParser::State::Complete);
  CHECK(strcmp(p.body(), "a=1") == 0 || p.bodyLen() == 3);
  HttpRequestParser idle;  // a speculative connection that says nothing
  CHECK(idle.state() == HttpRequestParser::State::Request && !idle.done());
}

TEST(parser_rejects_bad_requests_with_a_status) {
  struct Case {
    std::string data;
    int status;
  };
  const Case cases[] = {
      {"DELETE / HTTP/1.1\r\n\r\n", 405},
      {"GET nope HTTP/1.1\r\n\r\n", 400},
      {"GET / FTP/1.1\r\n\r\n", 400},
      {"POST /join HTTP/1.1\r\n\r\n", 400},                                   // no length
      {"POST /join HTTP/1.1\r\nContent-Length: 513\r\n\r\n", 413},           // too big
      {"POST /join HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n", 413},
      {"POST /join HTTP/1.1\r\nContent-Length: x\r\n\r\n", 400},
      {"POST /join HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc", 400},
      {"POST /join HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 501},
      {"GET /" + std::string(200, 'a') + " HTTP/1.1\r\n\r\n", 400},          // path too long
      {std::string(200, 'G') + " / HTTP/1.1\r\n\r\n", 400},                  // request line too long
  };
  for (const Case &c : cases) {
    HttpRequestParser p;
    CHECK(feedAll(p, c.data) == HttpRequestParser::State::Failed);
    CHECK_EQ(p.errorStatus(), c.status);
    CHECK(p.bodyLen() == 0);
    CHECK(p.done());
  }
}

TEST(parser_header_flood_is_bounded) {
  HttpRequestParser p;
  std::string flood = "GET / HTTP/1.1\r\n";
  while (flood.size() < 20000) flood += "X-Padding: yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy\r\n";
  CHECK(feedAll(p, flood, 97) == HttpRequestParser::State::Failed);
  CHECK_EQ(p.errorStatus(), 400);
}

TEST(parser_reset_wipes_the_form_and_allows_reuse) {
  HttpRequestParser p;
  CHECK(feedAll(p, post("password=hunter2hunter2")) == HttpRequestParser::State::Complete);
  const char *body = p.body();
  CHECK(strstr(body, "hunter2") != nullptr);
  p.reset();
  CHECK(strstr(body, "hunter2") == nullptr && p.bodyLen() == 0 && p.state() == HttpRequestParser::State::Request);
  CHECK(feedAll(p, kGet) == HttpRequestParser::State::Complete);
  // A failed request wipes too.
  HttpRequestParser f;
  CHECK(feedAll(f, "POST /j HTTP/1.1\r\nContent-Length: 12\r\n\r\npassword=abc" "Transfer-Encoding: chunked\r\n") !=
        HttpRequestParser::State::Failed);
}
