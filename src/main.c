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
#include <time.h>
#include "secrets.h"
#include "network.h"

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

int main(void)
{
    sleep_ms(1000);
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

    /* Enable the pullups on the GPIOs wired to the keys */
    gpio_set_dir(KEY0, GPIO_IN);
    gpio_pull_up(KEY0);
    gpio_set_dir(KEY1, GPIO_IN);
    gpio_pull_up(KEY1);

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
    
    /* Main loop */
    while (1) {
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
      // Font24->Width is 17, -> Height is 24 (obviously).
      // Font16->Width is 11, -> Height is 16.
      Paint_DrawString_EN(77, 3, "Sensoren im ZAM", &Font24, BLACK, WHITE);
      Paint_SelectImage(BlackImage);
      datetime_t cdt;
      rtc_get_datetime(&cdt);
      char spfbuf[32];
      if (cdt.year >= 2000) {
        sprintf(spfbuf, "%04d-%02d-%02d %02d:%02d:%02d",
                        cdt.year, cdt.month, cdt.day,
                        cdt.hour, cdt.min, cdt.sec);
      } else {
        sprintf(spfbuf, "NTP FAILED! Uptime: %lu min",
                to_ms_since_boot(get_absolute_time()) / 60000);
      }
      printf("E-Paper time shown: %s\r\n", spfbuf);
      Paint_DrawString_EN((400 - (strlen(spfbuf) * 11)) / 2,
                           300-17, spfbuf, &Font16, WHITE, BLACK);
      /* Now query and print sensors */
      int i = 0;
      while (stq[i].name != NULL) {
        sensdata sd = fetchtemphum(stq[i].host, stq[i].port);
        Paint_DrawString_EN(10, 35 + i*20, stq[i].name, &Font16, WHITE, BLACK);
        if (sd.isvalid) {
          sprintf(spfbuf, "%5.2f~C", sd.temp);
          Paint_DrawString_EN(220, 35 + i*20, spfbuf, &Font16, WHITE, BLACK);
          sprintf(spfbuf, "%5.1f%%", sd.hum);
          Paint_DrawString_EN(320, 35 + i*20, spfbuf, &Font16, WHITE, BLACK);
        } else {
          Paint_SelectImage(RedImage);
          Paint_DrawString_EN(220, 35 + i*20, "NO DATA", &Font16, WHITE, RED);
          Paint_SelectImage(BlackImage);
        }
        i++;
      }

#if EPAPERON
      /* Now display. */
      printf("Calling EPD_Display()\r\n");
      EPD_4IN2B_V2_Display(BlackImage, RedImage);
#endif /* EPAPERON */

      if ((gpio_get(KEY0) == 0) && (gpio_get(KEY1) == 0)) {
        printf("Both keys pressed, exiting.\n");
	break;
      }

      printf("Disconnecting from WiFi...\r\n");
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

      /* Sleep for one minute. */
      printf("Now sleeping for 60 seconds...\r\n");
      sleep_ms(60000);
    }
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
    DEV_Delay_ms(2000);//important, at least 2s
    DEV_Module_Exit();
#endif
    
    printf("main() will now return 0...\r\n");
    return 0;
}

