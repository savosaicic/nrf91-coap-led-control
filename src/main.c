#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>

#include "modem.h"

LOG_MODULE_REGISTER(coap_led, LOG_LEVEL_INF);

int main(void)
{
  int err;

  if (dk_leds_init() != 0) {
    LOG_ERR("Failed to initialize LEDs");
    return -ENODEV;
  }

  err = modem_configure();
  if (err) {
    LOG_ERR("Modem configuration failed: %d", err);
    return err;
  }

  while (1) {
  }

  return 0;
}
