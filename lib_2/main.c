#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "pin_mux.h"
#include "fsl_dma.h"
#include "fsl_i2c.h"
#include "fsl_wm8904.h"
#include "Audio.h"

#include "ff.h"
#include "diskio.h" 

extern int Flac_Play(const char filePath[]);

__asm void Delay_ms(unsigned int nCount)
{
0
		ldr  r1, =24000 // arm clock = 96000000hz
1
		subs r1, r1, #1
		bne  %B1
		subs r0, r0, #1
		bne  %B0
		bx	 lr
}

void Codec_Init(void)
{
	i2c_master_config_t i2cConfig;
    wm8904_config_t codecConfig;
    wm8904_handle_t codecHandle;

    /* I2C clock */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM4);
    /* reset FLEXCOMM for I2C */
    RESET_PeripheralReset(kFC4_RST_SHIFT_RSTn);	
    /*
     * enableMaster = true;
     * baudRate_Hz = 100000U;
     * enableTimeout = false;
     */
    I2C_MasterGetDefaultConfig(&i2cConfig);
    i2cConfig.baudRate_Bps = WM8904_I2C_BITRATE;
    I2C_MasterInit(I2C4, &i2cConfig, 12000000);
	
    WM8904_GetDefaultConfig(&codecConfig);
    codecHandle.i2c = I2C4;
    if (WM8904_Init(&codecHandle, &codecConfig) != kStatus_Success) {
        PRINTF("WM8904_Init failed!\r\n");
		return;
    }
    /* Adjust it to your needs, 0x0006 for -51 dB, 0x0039 for 0 dB etc. */
    WM8904_SetVolume(&codecHandle, 0x0020, 0x0020);	
}

void Gpio_Init(void)
{
    /* Define the init structure for the output LED pin*/
    gpio_pin_config_t led_config = {
        kGPIO_DigitalOutput, 0,
    };
	
    /* Init output LED GPIO. */
    GPIO_PinInit(GPIO, 0, 15, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 15, 0);
    GPIO_PinInit(GPIO, 0, 19, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 19, 0);
    GPIO_PinInit(GPIO, 0, 21, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 21, 0);
    GPIO_PinInit(GPIO, 0, 22, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 22, 0);
    GPIO_PinInit(GPIO, 0, 25, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 25, 1);
    GPIO_PinInit(GPIO, 0, 26, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 26, 1);
    GPIO_PinInit(GPIO, 0, 29, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 29, 1);
    GPIO_PinInit(GPIO, 0, 30, &led_config);
    GPIO_WritePinOutput(GPIO, 0, 30, 1);		
}

int main()
{
	FATFS fs;
	int Res;	

    /* Board pin, clock, debug console init */
    /* attach 12 MHz clock to FLEXCOMM0 (debug console) */
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);
    /* enable clock for GPIO*/
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio1);

    BOARD_InitPins();
    BOARD_BootClockFROHF96M();
    BOARD_InitDebugConsole();	
	
	Gpio_Init();
	DMA_Init(DMA0);
	I2S_Init();
	Codec_Init();
	Dmic_Init();
	
	f_mount(&fs, "4:", 0);	

	Res = 0;
	while (1) {
		if (Res == 0) {
			Res = Flac_Play("4:sky city.flac");
		}
	}
}
