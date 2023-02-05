#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "EPD_4in2b_V2.h"
#include "Debug.h"
#include <stdlib.h> // malloc() free()

/* define alias so we don't have to use the extremely long
 * and unintuitive names */
#define DISPSIZEX EPD_4IN2B_V2_WIDTH
#define DISPSIZEY EPD_4IN2B_V2_HEIGHT

int main(void)
{
    //stdio_init_all(); // DEV_Module_Init does that.
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

    //1.Draw black image
    printf("Draw black image\r\n");
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    Paint_DrawRectangle(0, 0, DISPSIZEX-1, 28, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(100, 2, "Sensoren im ZAM", &Font24, BLACK, WHITE);
    Paint_DrawString_EN(10, 35, "Tisch Suedhaus", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(240, 35, "14.55C", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(340, 35, "35.4%", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(10, 55, "Haupt-Toilette SH", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(240, 55, " 1.98C", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(340, 55, "67.2%", &Font16, WHITE, BLACK);
#if 0
    Paint_DrawPoint(10, 80, BLACK, DOT_PIXEL_1X1, DOT_STYLE_DFT);
    Paint_DrawPoint(10, 90, BLACK, DOT_PIXEL_2X2, DOT_STYLE_DFT);
    Paint_DrawPoint(10, 100, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
    Paint_DrawPoint(10, 110, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
    Paint_DrawLine(20, 70, 70, 120, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(70, 70, 20, 120, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);      
    Paint_DrawRectangle(20, 70, 70, 120, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(80, 70, 130, 120, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(10, 0, "waveshare", &Font16, BLACK, WHITE);    
    Paint_DrawNum(10, 50, 987654321, &Font16, WHITE, BLACK);
    //2.Draw red image
    printf("Draw red image\r\n");
    Paint_SelectImage(RedImage);
    Paint_Clear(WHITE);
    Paint_DrawCircle(160, 95, 20, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawCircle(210, 95, 20, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawLine(85, 95, 125, 95, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    Paint_DrawLine(105, 75, 105, 115, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);  
    Paint_DrawString_EN(10, 20, "hello world", &Font12, WHITE, BLACK);
    Paint_DrawNum(10, 33, 123456789, &Font12, BLACK, WHITE);
#endif

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
    
    return 0;
}

