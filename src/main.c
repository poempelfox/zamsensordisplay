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
#include "secrets.h"
#include "network.h"

/* define alias so we don't have to use the extremely long
 * and unintuitive names */
#define DISPSIZEX EPD_4IN2B_V2_WIDTH
#define DISPSIZEY EPD_4IN2B_V2_HEIGHT


int main(void)
{
    stdio_init_all(); // DEV_Module_Init does that, so we normally don't.
    /* Prepare RTC for use later */
    rtc_init();

    sleep_ms(1000);

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
    while (1) {
      printf("Connecting to WiFi...\r\n");
      int wifierr = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
      //int wifierr = cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
      if (wifierr != 0) {
        printf("failed to connect, Error code: %d.\r\n", wifierr);
      } else {
        printf("Connected.\n");
      }
      dns_init();
      settimefromntp("ntp3.fau.de");
      fetchtemphum("fox.poempelfox.de", 7777);
      fetchtemphum("fox.poempelfox.de", 7778);

      sleep_ms(20000);

      printf("Disconnecting from WiFi...\r\n");
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

      sleep_ms(20000);
    }
    /* Once you called this, you can never cyw43_arch_init again, it
     * will just hang. Not that it's documented that way of course,
     * but it's a buggy POS. */
    cyw43_arch_deinit();

#if 0 /* No epaper for now.
    DEV_Delay_ms(500); 

    if (DEV_Module_Init() != 0) {
      printf("DEV_Module_Init() failed! No ePaper to control!\n");
      return -1;
    }

    printf("e-Paper Init and Clear...\r\n");
    EPD_4IN2B_V2_Init();
    EPD_4IN2B_V2_Clear();
    DEV_Delay_ms(500);

    /* Create a new image cache named BlackImage and RedImage and fill both with white */
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

    // Select Image
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    Paint_SelectImage(RedImage);
    Paint_Clear(WHITE);

    printf("Draw image\r\n");
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    Paint_DrawRectangle(0, 0, DISPSIZEX-1, 28, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    // Font24->Width is 17, -> Height is 24 (obviously).
    Paint_DrawString_EN(77, 3, "Sensoren im ZAM", &Font24, BLACK, WHITE);
    // Font16->Width is 11, -> Height is 16.
    Paint_DrawString_EN(10, 35, "Tisch Suedhaus", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(240, 35, "14.55C", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(340, 35, "35.4%", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(10, 55, "Haupt-Toilette SH", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(240, 55, " 1.98C", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(340, 55, "67.2%", &Font16, WHITE, BLACK);

    printf("EPD_Display\r\n");
    EPD_4IN2B_V2_Display(BlackImage, RedImage);
    DEV_Delay_ms(2000);
	    
    /* Enable the pullups on the GPIOs wired to the keys */
    gpio_set_dir(KEY0, GPIO_IN);
    gpio_pull_up(KEY0);
    gpio_set_dir(KEY1, GPIO_IN);
    gpio_pull_up(KEY1);
    DEV_Delay_ms(500);
	
    while (1) {
      /* Loop until both keys are pressed. */
      if ((DEV_Digital_Read(KEY0) == 0) && (DEV_Digital_Read(KEY1) == 0)) {
        printf("Both keys pressed, exiting.\n");
	break;
      }
    }

    printf("Test ending, Clearing display...\r\n");
    EPD_4IN2B_V2_Clear();

    printf("Goto Sleep...\r\n");
    EPD_4IN2B_V2_Sleep();
    free(BlackImage);
    free(RedImage);
    BlackImage = NULL;
    RedImage = NULL;
    DEV_Delay_ms(2000);//important, at least 2s
    // close 5V
    printf("close 5V, Module enters 0 power consumption ...\r\n");
    DEV_Module_Exit();
#endif
    
    return 0;
}

