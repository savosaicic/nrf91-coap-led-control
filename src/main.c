#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>

#include "modem.h"

LOG_MODULE_REGISTER(coap_led, LOG_LEVEL_INF);

#define CONFIG_COAP_SERVER_HOSTNAME "your-domain.com"
#define CONFIG_COAP_SERVER_PORT     5683

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

int main(void)
{
  int             err;
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
  }

  return 0;
}
