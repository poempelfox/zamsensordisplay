#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "EPD_4in2b_V2.h"
#include "Debug.h"
#include <stdlib.h> // malloc() free()
#include <pico/cyw43_arch.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/dns.h>
#include <lwip/timeouts.h>
#include <hardware/rtc.h>
#include <hardware/adc.h>
#include <time.h>
#include "secrets.h"
#include "network.h"

/* This gets set to >0 by interrupt handler if both buttons were
 * pressed at the same time. */
volatile int pleaseexit = 0;

/* Quick define to disable compiling in control of the ePaper
 * (for testing powersave / network) */
#define EPAPERON 1

/* define alias so we don't have to use the extremely long
 * and unintuitive names */
#define DISPSIZEX EPD_4IN2B_V2_WIDTH
#define DISPSIZEY EPD_4IN2B_V2_HEIGHT

struct remsens { // Remote Sensors
  char * name;
  char * host;
  uint16_t port;
};

struct remsens stq[] = {
  { .name = "Konferenztisch", .host = "fox.poempelfox.de", 7777 },
  { .name = "EG Mitte", .host = "tempzamsh.im.zam.haus", 7337 },
  { .name = "1. OG", .host = "tempzamsh.im.zam.haus", 7340 },
  { .name = "Toilette", .host = "tempzamsh.im.zam.haus", 7343 },
  { .name = "Aquarium-NH", .host = "tempzamnh.im.zam.haus", 7342 },
  { .name = "Serverraum", .host = "tempzamnh.im.zam.haus", 7344 },
  { .name = "Test2", .host = "fox.poempelfox.de", 7778 },
  { .name = NULL, .host = NULL, 0 }
};

void keyirqcb(unsigned int gpio, uint32_t event_mask)
{
  static int k0state = 0;
  static int k1state = 0;
  printf("KeyboardIRQCallback: gpio=%u, event_mask=%u\r\n", gpio, event_mask);
  if (gpio == KEY0) {
    if (event_mask & GPIO_IRQ_EDGE_FALL) {
      k0state = 1;
    } else { /* GPIO_IRQ_EDGE_RISE */
      k0state = 0;
    }
  }
  if (gpio == KEY1) {
    if (event_mask & GPIO_IRQ_EDGE_FALL) {
      k1state = 1;
    } else { /* GPIO_IRQ_EDGE_RISE */
      k1state = 0;
    }
  }
  if ((k0state == 1) && (k1state == 1)) {
    printf("Both keys pressed, setting pleaseexit.\r\n");
    pleaseexit = 1;
  }
  // if ((gpio_get(KEY0) == 0) && (gpio_get(KEY1) == 0)) {
  // Note: because we use the simple high-level function,
  // calling gpio_acknowledge_irq() is not necessary.
}

/* Read VSYS. This is a veritable mess on the Pico W, because the ADC pin for
 * VSYS is suddenly shared with the Wifi. Of course, at the time of writing
 * this, this is neither properly documented nor is there any support for
 * that mess in the SDK. This routine was compiled from code snippets in
 * bugreports and proposed patches on github. */
float readvsys()
{
  cyw43_thread_enter();
  adc_gpio_init(29); /* GPIO 29 == ADC 3 */
  adc_select_input(3);
  uint32_t vsysraw = 0;
  for (int i = 0; i < 4; i++) { // Sample a few times because the ADC is really bad
    vsysraw += adc_read();
  }
  cyw43_thread_exit();
  /* Now undo what adc_gpio_init did, so it can be used for WiFi again. */
  gpio_set_function(29, GPIO_FUNC_PIO1);
  gpio_pull_down(29);
  gpio_set_slew_rate(29, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(29, GPIO_DRIVE_STRENGTH_12MA);
  vsysraw = vsysraw / 4;
  /* VSYS is connected through a 100k / 200kOhm voltage divider. Thus what
   * the ADC sees is 1/3 of the voltage of VSYS. */
  float vsys = 3.0 * vsysraw * (3.3 / 4095.0);
  printf("VSYS: raw ADC value %u, calculated VSYS: %.2f\r\n", vsysraw, vsys);
  return vsys;
}

int main(void)
{
    sleep_ms(1500);
#if EPAPERON
    if (DEV_Module_Init() != 0) {
      printf("DEV_Module_Init() failed! No ePaper to control!\n");
      return -1;
    }
    printf("e-Paper Init and Clear...\r\n");
    EPD_4IN2B_V2_Init();
    EPD_4IN2B_V2_Clear();
#else /* !EPAPERON */
    stdio_init_all(); // DEV_Module_Init does that, so we normally don't.
#endif
    /* Create a new image cache named BlackImage and RedImage */
    uint8_t * BlackImage; uint8_t * RedImage; // Red or Yellow - red on our display.
    UWORD Imagesize = ((DISPSIZEX % 8 == 0)
                       ? (DISPSIZEX / 8)
                       : (DISPSIZEX / 8 + 1)) * DISPSIZEY;
    if ((BlackImage = (uint8_t *)malloc(Imagesize)) == NULL) {
        printf("Failed to allocate memory for black image...\r\n");
        return -1;
    }
    if ((RedImage = (uint8_t *)malloc(Imagesize)) == NULL) {
        printf("Failed to allocate memory for red image...\r\n");
        return -1;
    }
    printf("NewImage: BlackImage and RYImage\r\n");
    // the last parameter is 'if image is inverted', so not sure the WHITE makes
    // sense here. OTOH, I cannot find this being used anywhere. AFAICT this
    // gets put into an internal variable that is never read.
    Paint_NewImage(BlackImage, EPD_4IN2B_V2_WIDTH, EPD_4IN2B_V2_HEIGHT, ROTATE_0, WHITE);
    Paint_NewImage(RedImage, EPD_4IN2B_V2_WIDTH, EPD_4IN2B_V2_HEIGHT, ROTATE_0, WHITE);

    /* Prepare RTC for use later */
    rtc_init();
    /* Prepare ADC for use later */
    adc_init();
    /* Configure the GPIOs wired to the keys */
    gpio_set_dir(KEY0, GPIO_IN);
    gpio_pull_up(KEY0);
    gpio_set_dir(KEY1, GPIO_IN);
    gpio_pull_up(KEY1);
    /* We'll attach the interrupt handlers later, after WiFi init,
     * that makes sure the pullups have already pulled the GPIO high */

    printf("Initializing WiFi...\n");
    /* WiFi test */
#ifndef WIFI_COUNTRY
    if (cyw43_arch_init()) {
#else
    if (cyw43_arch_init_with_country(WIFI_COUNTRY)) {
#endif
      printf("Initializing WiFi failed! That's sad!\r\n");
      return 1;
    }
    cyw43_arch_enable_sta_mode();
    uint8_t mymac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, &mymac[0]);
    printf("My WiFi MAC-address is: ");
    for (int i = 0; i < 6; i++) {
      printf("%02x%s", mymac[i], (i == 5) ? "\n" : ":");
    }
    dns_init();
    absolute_time_t nextntpatt = make_timeout_time_ms(0);

    /* Attach IRQ handler for the keys */
    gpio_set_irq_enabled_with_callback(KEY0, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, keyirqcb);
    gpio_set_irq_enabled(KEY1, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    
    /* Main loop */
    do {
      printf("Connecting to WiFi...\r\n");
      int wifierr = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
      //int wifierr = cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
      if (wifierr != 0) {
        printf("failed to connect, Error code: %d.\r\n", wifierr);
      } else {
        printf("Connected.\n");
      }
      if (get_absolute_time() > nextntpatt) {
        /* Time to try to get the time via NTP. */
        int ntpres = settimefromntp("ntp3.fau.de");
        if (ntpres != 0) {
          ntpres = settimefromntp("ntp2.fau.de");
          if (ntpres != 0) {
            ntpres = settimefromntp("pool.ntp.org");
          }
        }
        if (ntpres == 0) { // we did successfully set time
          nextntpatt = make_timeout_time_ms(3600 * 1000);
        } else { // we did not get time, try again in 10 min
          nextntpatt = make_timeout_time_ms(600 * 1000);
        }
      }

      /* Update the e-Paper display */
      printf("Preparing image to display on ePaper...\r\n");
      // Clear image
      Paint_SelectImage(BlackImage);
      Paint_Clear(WHITE);
      Paint_SelectImage(RedImage);
      Paint_Clear(WHITE);
      Paint_DrawRectangle(0, 0, DISPSIZEX-1, 28, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
      // Font24.Width is 17, .Height is 24 (obviously).
      // Font16.Width is 11, .Height is 16.
      // TerminusFont16.Width is 8, .Height is 16.
      // TerminusFont19.Width is 10, .Height is 19.
      Paint_DrawString_EN(77, 3, "Sensoren im ZAM", &Font24, BLACK, WHITE);
      datetime_t cdt;
      rtc_get_datetime(&cdt);
      uint8_t spfbuf[32];
      if (cdt.year >= 2000) {
        sprintf(spfbuf, "%04d-%02d-%02d %02d:%02d:%02d",
                        cdt.year, cdt.month, cdt.day,
                        cdt.hour, cdt.min, cdt.sec);
      } else {
        sprintf(spfbuf, "NTP FAILED! Uptime: %lu min",
                to_ms_since_boot(get_absolute_time()) / 60000);
      }
      printf("E-Paper time shown: %s\r\n", spfbuf);
      Paint_DrawRectangle(0, DISPSIZEY-23, DISPSIZEX-1, DISPSIZEY-1, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
      Paint_DrawString_EN((DISPSIZEX - 3 - (strlen(spfbuf) * FontTerminus19.Width)),
                           DISPSIZEY-21, spfbuf, &FontTerminus19, BLACK, WHITE);
      /* Now try to read VSYS through the ADC. This is complicated,
       * because other than on the Pico, on the Pico W the pin for
       * that is suddenly shared with the WiFi m(
       * We have put the resulting mess into its own function. */
      float vsys = readvsys();
      sprintf(spfbuf, "%.2fV", vsys);
      Paint_DrawString_EN(3, DISPSIZEY-21, spfbuf, &FontTerminus19, BLACK, WHITE);
      /* Now switch back to painting in the black image */
      Paint_SelectImage(BlackImage);
      /* Now query and print sensors */
      int i = 0;
      while (stq[i].name != NULL) {
        sensdata sd = fetchtemphum(stq[i].host, stq[i].port);
        Paint_DrawString_EN(10, 35 + i*22, stq[i].name, &FontTerminus19, WHITE, BLACK);
        if (sd.isvalid) {
          sprintf(spfbuf, "%5.2f" "\x7f" "C", sd.temp);
          Paint_DrawString_EN(220, 35 + i*22, spfbuf, &FontTerminus19, WHITE, BLACK);
          sprintf(spfbuf, "%5.1f%%", sd.hum);
          Paint_DrawString_EN(DISPSIZEX - 11 - (strlen(spfbuf) * FontTerminus19.Width),
                              35 + i*22, spfbuf, &FontTerminus19, WHITE, BLACK);
        } else {
          Paint_SelectImage(RedImage);
          Paint_DrawString_EN(220, 35 + i*22, "NO DATA", &FontTerminus19, WHITE, RED);
          Paint_SelectImage(BlackImage);
        }
        i++;
      }

#if EPAPERON
      /* Now display. */
      printf("Calling EPD_Display()\r\n");
      EPD_4IN2B_V2_Display(BlackImage, RedImage);
#endif /* EPAPERON */

      printf("Disconnecting from WiFi...\r\n");
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

      /* Sleep for one minute. */
      printf("Now sleeping for 60 seconds...\r\n");
      absolute_time_t sl60end = make_timeout_time_ms(60000);
      uint32_t wakeupcntr = 0;
      while ((pleaseexit == 0) && (best_effort_wfe_or_timeout(sl60end) == false)) {
        wakeupcntr++;
      }
      printf("wait-routine was awoken %u times\r\n", wakeupcntr);
    } while (pleaseexit == 0);
    printf("Exited main loop.\r\n");
    /* Once you called this, you can never cyw43_arch_init again, it
     * will just hang. Not that it's documented that way of course,
     * but it's a buggy POS. */
    cyw43_arch_deinit();

#if EPAPERON
    printf("Ending, Clearing display...\r\n");
    EPD_4IN2B_V2_Clear();
    printf("Goto Sleep...\r\n");
    EPD_4IN2B_V2_Sleep();
    free(BlackImage);
    free(RedImage);
    BlackImage = NULL;
    RedImage = NULL;
    DEV_Delay_ms(2000); // important, at least 2s - says the WS example without ever explaining why.
    DEV_Module_Exit();
#endif
    
    printf("main() will now return 0...\r\n");
    return 0;
}

