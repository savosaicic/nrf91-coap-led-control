#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/random/random.h>

#include "modem.h"

LOG_MODULE_REGISTER(coap_led, LOG_LEVEL_INF);

#define CONFIG_COAP_SERVER_HOSTNAME            "your-domain.com"
#define CONFIG_COAP_SERVER_PORT                5683
#define CONFIG_COAP_OBSERVE_REREGISTER_SECONDS 60

#define COAP_BUF_SIZE 256

static uint8_t coap_tx[COAP_BUF_SIZE];
static uint8_t coap_rx[COAP_BUF_SIZE];

static void led_apply(uint8_t state)
{
  if (state) {
    dk_set_led_on(DK_LED2);
  } else {
    dk_set_led_off(DK_LED2);
  }
  LOG_INF("LED → %s", state ? "ON" : "OFF");
}

static int resolve_server(struct sockaddr *out)
{
  char            port_str[6];
  struct addrinfo hints = {
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_DGRAM,
    .ai_protocol = IPPROTO_UDP,
  };
  struct addrinfo *res = NULL;

  snprintk(port_str, sizeof(port_str), "%d", CONFIG_COAP_SERVER_PORT);

  int ret = getaddrinfo(CONFIG_COAP_SERVER_HOSTNAME, port_str, &hints, &res);
  if (ret != 0) {
    LOG_ERR("getaddrinfo(%s): %d", CONFIG_COAP_SERVER_HOSTNAME, ret);
    return -ENOENT;
  }

  memcpy(out, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);

  char addr_str[NET_IPV4_ADDR_LEN];
  net_addr_ntop(AF_INET, &net_sin(out)->sin_addr, addr_str, sizeof(addr_str));
  LOG_INF("Resolved %s: %s:%d", CONFIG_COAP_SERVER_HOSTNAME, addr_str,
          CONFIG_COAP_SERVER_PORT);

  return 0;
}

static int send_observe_request(int sock, const struct sockaddr *server,
                                uint8_t *token, uint8_t tkl)
{
  struct coap_packet req;
  uint16_t           msg_id;
  uint8_t            obs;
  int                ret;

  msg_id = sys_rand32_get() & 0xFFFF;
  obs    = 0;

  ret = coap_packet_init(&req, coap_tx, sizeof(coap_tx), COAP_VERSION_1,
                         COAP_TYPE_CON, tkl, token, COAP_METHOD_GET, msg_id);
  if (ret < 0) {
    return ret;
  }

  ret = coap_packet_append_option(&req, COAP_OPTION_OBSERVE, &obs, sizeof(obs));
  if (ret < 0) {
    return ret;
  }

  ret =
    coap_packet_append_option(&req, COAP_OPTION_URI_PATH, "led", strlen("led"));
  if (ret < 0) {
    return ret;
  }

  ret = sendto(sock, req.data, req.offset, 0, server, sizeof(struct sockaddr));
  if (ret < 0) {
    LOG_ERR("sendto: %d", errno);
    return -errno;
  }

  LOG_INF("Observe registration sent (id=0x%04x)", msg_id);
  return 0;
}

static int send_ack(int sock, const struct sockaddr *server, uint16_t msg_id,
                    uint8_t *token, uint8_t tkl)
{
  struct coap_packet ack;
  int                ret;

  ret = coap_packet_init(&ack, coap_tx, sizeof(coap_tx), COAP_VERSION_1,
                         COAP_TYPE_ACK, tkl, token, COAP_CODE_EMPTY, msg_id);
  if (ret < 0) {
    return ret;
  }

  return sendto(sock, ack.data, ack.offset, 0, server, sizeof(struct sockaddr));
}

/*
 * Re-send the Observe registration if the deadline has passed.
 * Updates *last_reg_ms on success.
 * Returns 0 on success, negative on send failure.
 */
static int maybe_reregister(int sock, const struct sockaddr *server,
                            uint8_t *token, uint8_t tkl, int64_t *last_reg_ms,
                            int64_t rereg_ms)
{
  int64_t elapsed;

  elapsed = k_uptime_get() - *last_reg_ms;
  if (elapsed < rereg_ms) {
    return 0;
  }

  LOG_INF("Re-registering observer...");
  int ret = send_observe_request(sock, server, token, tkl);
  if (ret < 0) {
    return ret;
  }

  *last_reg_ms = k_uptime_get();
  return 0;
}

static int receive_coap_packet(int sock, struct coap_packet *pkt,
                               struct coap_option *options, uint8_t opt_count,
                               struct sockaddr *src, socklen_t *src_len)
{
  int received;
  int ret;

  received = recvfrom(sock, coap_rx, sizeof(coap_rx), 0, src, src_len);
  if (received < 0) {
    LOG_ERR("recvfrom: %d", errno);
    return -errno;
  }

  ret = coap_packet_parse(pkt, coap_rx, received, options, opt_count);
  if (ret < 0) {
    LOG_WRN("Bad CoAP packet: %d", ret);
    return ret;
  }

  return received;
}

static int handle_packet(int sock, const struct sockaddr *src,
                         const struct coap_packet *pkt,
                         const uint8_t *session_token, uint8_t session_tkl)
{
  uint16_t       id;
  uint8_t        code;
  uint8_t        type;
  uint8_t        rx_tkl;
  const uint8_t *payload;
  uint16_t       payload_len;
  uint8_t        rx_token[COAP_TOKEN_MAX_LEN];

  rx_tkl = coap_header_get_token(pkt, rx_token);
  if (rx_tkl != session_tkl ||
      memcmp(rx_token, session_token, session_tkl) != 0) {
    LOG_WRN("Unknown token, ignoring");
    return -EBADMSG;
  }

  code = coap_header_get_code(pkt);
  type = coap_header_get_type(pkt);
  id   = coap_header_get_id(pkt);

  if (type == COAP_TYPE_CON) {
    send_ack(sock, src, id, rx_token, rx_tkl);
  }

  if (code == COAP_RESPONSE_CODE_CONTENT) {
    payload = coap_packet_get_payload(pkt, &payload_len);
    if (payload && payload_len >= 1) {
      led_apply(payload[0] == '1' ? 1 : 0);
    }
  } else {
    LOG_WRN("Unexpected code 0x%02x", code);
  }

  return 0;
}

static void observe_loop(int sock, const struct sockaddr *server)
{
  int                ret;
  struct pollfd      pfd;
  struct sockaddr    src;
  struct coap_packet pkt;
  uint32_t           rnd;
  socklen_t          src_len;
  int64_t            rereg_ms;
  uint8_t            token[4];
  int64_t            remaining;
  struct coap_option options[8];
  int                timeout_ms;
  int64_t            last_reg_ms;

  rnd = sys_rand32_get();
  memcpy(token, &rnd, sizeof(token));

  if (send_observe_request(sock, server, token, sizeof(token)) < 0) {
    return;
  }

  last_reg_ms = k_uptime_get();
  rereg_ms    = CONFIG_COAP_OBSERVE_REREGISTER_SECONDS * 1000;

  pfd.fd     = sock;
  pfd.events = POLLIN;

  while (1) {
    if (maybe_reregister(sock, server, token, sizeof(token), &last_reg_ms,
                         rereg_ms) < 0) {
      return;
    }

    remaining  = rereg_ms - (k_uptime_get() - last_reg_ms);
    timeout_ms = (int)(remaining > INT32_MAX ? INT32_MAX : remaining);

    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
      LOG_ERR("poll: %d", errno);
      return;
    }
    if (ret == 0) {
      continue;
    }

    src_len = sizeof(src);
    ret = receive_coap_packet(sock, &pkt, options, ARRAY_SIZE(options), &src,
                              &src_len);
    if (ret < 0) {
      return;
    }

    handle_packet(sock, &src, &pkt, token, sizeof(token));
  }
}

int main(void)
{
  int             err;
  int             sock;
  struct sockaddr server;

  if (dk_leds_init() != 0) {
    LOG_ERR("Failed to initialize LEDs");
    return -ENODEV;
  }

  err = modem_configure();
  if (err) {
    LOG_ERR("Modem configuration failed: %d", err);
    return err;
  }

  if (resolve_server(&server) < 0) {
    LOG_ERR("Failed to resolve server address");
    return -1;
  }

  while (1) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
      LOG_ERR("Failed to create socket: %d", errno);
      return -1;
    }

    observe_loop(sock, &server);

    close(sock);
    LOG_WRN("Reconnecting in 5s...");
    k_sleep(K_SECONDS(5));
  }

  return 0;
}
