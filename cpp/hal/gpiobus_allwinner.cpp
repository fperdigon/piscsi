//---------------------------------------------------------------------------
//
//	SCSI Target Emulator RaSCSI Reloaded
//	for Raspberry Pi (And Banana Pi)
//
//  Copyright (c) 2012-2015 Ben Croston
//  Copyright (C) 2022 akuker
// 
//  Large portions of this functionality were derived from c_gpio.c, which
//  is part of the RPI.GPIO library available here:
//     https://github.com/BPI-SINOVOIP/RPi.GPIO/blob/master/source/c_gpio.c
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy of
//  this software and associated documentation files (the "Software"), to deal in
//  the Software without restriction, including without limitation the rights to
//  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
//  of the Software, and to permit persons to whom the Software is furnished to do
//  so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//
//---------------------------------------------------------------------------

#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "hal/gpiobus_allwinner.h"
#include "hal/gpiobus.h"
#include "hal/pi_defs/bpi-gpio.h"
#include "log.h"

extern int wiringPiMode;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

// #pragma GCC diagnostic ignored "-Wformat-truncation"

#define GPIO_BANK(pin)  ((pin) >> 5)
#define GPIO_NUM(pin)   ((pin) & 0x1F)

#define GPIO_CFG_INDEX(pin)     (((pin) & 0x1F) >> 3)
#define GPIO_CFG_OFFSET(pin)    ((((pin) & 0x1F) & 0x7) << 2)

#define GPIO_PUL_INDEX(pin)     (((pin) & 0x1F )>> 4) 
#define GPIO_PUL_OFFSET(pin)    (((pin) & 0x0F) << 1)

#define SUNXI_R_GPIO_BASE	0x01F02000
#define SUNXI_R_GPIO_REG_OFFSET   0xC00
#define SUNXI_GPIO_BASE		0x01C20000
#define SUNXI_GPIO_REG_OFFSET   0x800
#define SUNXI_CFG_OFFSET	0x00
#define SUNXI_DATA_OFFSET	0x10
#define SUNXI_PUD_OFFSET	0x1C
#define SUNXI_BANK_SIZE		0x24

#define MAP_SIZE        (4096*2)
#define MAP_MASK        (MAP_SIZE - 1)


// TODO: Delete these. They're just to make things compile for now
int pinToGpio_BPI_M1P [64] = {0};
int pinToGpio_BPI_M2 [64] = {0};
int pinToGpio_BPI_M2M [64] = {0};
int pinToGpio_BPI_M2M_1P1 [64] = {0};
int pinToGpio_BPI_M2P [64] = {0};
int pinToGpio_BPI_M2U [64] = {0};
int pinToGpio_BPI_M3 [64] = {0};
int pinToGpio_BPI_M64 [64] = {0};
int pinTobcm_BPI_M1P [64] = {0};
int pinTobcm_BPI_M2 [64] = {0};
int pinTobcm_BPI_M2M [64] = {0};
int pinTobcm_BPI_M2M_1P1 [64] = {0};
int pinTobcm_BPI_M2P [64] = {0};
int pinTobcm_BPI_M2U [64] = {0};
int pinTobcm_BPI_M3 [64] = {0};
int pinTobcm_BPI_M64 [64] = {0};

// #define BCM2708_PERI_BASE_DEFAULT   0x20000000
// #define BCM2709_PERI_BASE_DEFAULT   0x3f000000
// #define GPIO_BASE_OFFSET            0x200000
#define FSEL_OFFSET                 0   // 0x0000
#define SET_OFFSET                  7   // 0x001c / 4
#define CLR_OFFSET                  10  // 0x0028 / 4
#define PINLEVEL_OFFSET             13  // 0x0034 / 4
#define EVENT_DETECT_OFFSET         16  // 0x0040 / 4
#define RISING_ED_OFFSET            19  // 0x004c / 4
#define FALLING_ED_OFFSET           22  // 0x0058 / 4
#define HIGH_DETECT_OFFSET          25  // 0x0064 / 4
#define LOW_DETECT_OFFSET           28  // 0x0070 / 4
#define PULLUPDN_OFFSET             37  // 0x0094 / 4
#define PULLUPDNCLK_OFFSET          38  // 0x0098 / 4

#define PAGE_SIZE  (4*1024)
#define BLOCK_SIZE (4*1024)

// BpiBoardsType bpiboard[];
GPIOBUS_Allwinner::BpiBoardsType GPIOBUS_Allwinner::bpiboard [] = 
{
  { "bpi-0",	      -1, 0, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-1",	      -1, 1, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-2",	      -1, 2, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-3",	      -1, 3, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-4",	      -1, 4, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-5",	      -1, 5, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-6",	      -1, 6, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-7",	      -1, 7, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-8",	      -1, 8, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-9",	      -1, 9, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-10",	      -1, 10, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-11",	      -1, 11, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-12",	      -1, 12, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-13",	      -1, 13, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-14",	      -1, 14, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-15",	      -1, 15, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-new",	      -1, 16, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-x86",	      -1, 17, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-rpi",	      -1, 18, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-rpi2",	      -1, 19, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-rpi3",	      -1, 20, 1, 2, 5, 0, NULL, NULL, NULL 	},
  { "bpi-m1",	   10001, 21, 1, 2, 5, 0, pinToGpio_BPI_M1P, physToGpio_BPI_M1P, pinTobcm_BPI_M1P 	},
  { "bpi-m1p",	   10001, 22, 1, 2, 5, 0, pinToGpio_BPI_M1P, physToGpio_BPI_M1P, pinTobcm_BPI_M1P 	},
  { "bpi-r1",	   10001, 23, 1, 2, 5, 0, pinToGpio_BPI_M1P, physToGpio_BPI_M1P, pinTobcm_BPI_M1P 	},
  { "bpi-m2",	   10101, 24, 1, 2, 5, 0, pinToGpio_BPI_M2, physToGpio_BPI_M2, pinTobcm_BPI_M2 	},
  { "bpi-m3",	   10201, 25, 1, 3, 5, 0, pinToGpio_BPI_M3, physToGpio_BPI_M3, pinTobcm_BPI_M3 	},
  { "bpi-m2p",	   10301, 26, 1, 2, 5, 0, pinToGpio_BPI_M2P, physToGpio_BPI_M2P, pinTobcm_BPI_M2P 	},
  { "bpi-m64",	   10401, 27, 1, 3, 5, 0, pinToGpio_BPI_M64, physToGpio_BPI_M64, pinTobcm_BPI_M64 	},
  { "bpi-m2u",	   10501, 28, 1, 3, 5, 0, pinToGpio_BPI_M2U, physToGpio_BPI_M2U, pinTobcm_BPI_M2U 	},
  { "bpi-m2m",	   10601, 29, 1, 1, 5, 0, pinToGpio_BPI_M2M, physToGpio_BPI_M2M, pinTobcm_BPI_M2M 	},
  { "bpi-m2p_H2+", 10701, 30, 1, 2, 5, 0, pinToGpio_BPI_M2P, physToGpio_BPI_M2P, pinTobcm_BPI_M2P 	},
  { "bpi-m2p_H5",  10801, 31, 1, 2, 5, 0, pinToGpio_BPI_M2P, physToGpio_BPI_M2P, pinTobcm_BPI_M2P 	},
  { "bpi-m2u_V40", 10901, 32, 1, 3, 5, 0, pinToGpio_BPI_M2U, physToGpio_BPI_M2U, pinTobcm_BPI_M2U 	},
  { "bpi-m2z",	   11001, 33, 1, 1, 5, 0, pinToGpio_BPI_M2P, physToGpio_BPI_M2P, pinTobcm_BPI_M2P 	},
//   { "bpi-r2",      11101, 34, 1, 3, 5, 0, pinToGpio_BPI_R2,  physToGpio_BPI_R2,  pinTobcm_BPI_R2    },
  { NULL,		0, 0, 1, 2, 5, 0, NULL, NULL, NULL 	},
} ;



bool GPIOBUS_Allwinner::Init(mode_e mode, board_type::rascsi_board_type_e rascsi_type)
{
    GPIOBUS::Init(mode, rascsi_type);
    sunxi_setup();



//     wiringPiSetup();
//     wiringPiMode = WPI_MODE_GPIO;


// 	LOGTRACE("%s Set Drive Strength", __PRETTY_FUNCTION__);
// 	// Set Drive Strength to 16mA
// 	DrvConfig(7);

// 	// Set pull up/pull down
// 	LOGTRACE("%s Set pull up/down....", __PRETTY_FUNCTION__);

// #if SIGNAL_CONTROL_MODE == 0
// 	int pullmode = GPIO_PULLNONE;
// #elif SIGNAL_CONTROL_MODE == 1
// 	int pullmode = GPIO_PULLUP;
// #else
// 	int pullmode = GPIO_PULLDOWN;
// #endif

// 	// Initialize all signals
// 	LOGTRACE("%s Initialize all signals....", __PRETTY_FUNCTION__);

// 	for (int i = 0; SignalTable[i] >= 0; i++) {
// 		int j = SignalTable[i];
// 		PinSetSignal(j, OFF);
// 		PinConfig(j, board_type::gpio_high_low_e::GPIO_INPUT);
// 		PullConfig(j, pullmode);
// 	}

// 	// Set control signals
// 	LOGTRACE("%s Set control signals....", __PRETTY_FUNCTION__);
// 	PinSetSignal(PIN_ACT, OFF);
// 	PinSetSignal(PIN_TAD, OFF);
// 	PinSetSignal(PIN_IND, OFF);
// 	PinSetSignal(PIN_DTD, OFF);
// 	PinConfig(PIN_ACT, board_type::gpio_high_low_e::GPIO_OUTPUT);
// 	PinConfig(PIN_TAD, board_type::gpio_high_low_e::GPIO_OUTPUT);
// 	PinConfig(PIN_IND, board_type::gpio_high_low_e::GPIO_OUTPUT);
// 	PinConfig(PIN_DTD, board_type::gpio_high_low_e::GPIO_OUTPUT);

// 	// Set the ENABLE signal
// 	// This is used to show that the application is running
// 	PinSetSignal(PIN_ENB, ENB_OFF);
// 	PinConfig(PIN_ENB, board_type::gpio_high_low_e::GPIO_OUTPUT);



// 	// Create work table

// 	LOGTRACE("%s MakeTable....", __PRETTY_FUNCTION__);
// 	MakeTable();

// 	// Finally, enable ENABLE
// 	LOGTRACE("%s Finally, enable ENABLE....", __PRETTY_FUNCTION__);
// 	// Show the user that this app is running
// 	SetControl(PIN_ENB, ENB_ON);

//     // if(!SetupPollSelectEvent()){
//     //     LOGERROR("Failed to setup SELECT poll event");
//     //     return false;
//     // }

	return true;



    // SetMode(PIN_IND, OUTPUT);
    // SetMode(PIN_TAD, OUTPUT);
    // SetMode(PIN_DTD, OUTPUT);
    // PullConfig(PIN_IND, GPIO_PULLUP);
    // PullConfig(PIN_IND, GPIO_PULLUP);
    // PullConfig(PIN_IND, GPIO_PULLUP);


    // SetMode(PIN_DT0, OUTPUT);
    // SetMode(PIN_DT1, OUTPUT);
    // SetMode(PIN_DT2, OUTPUT);
    // SetMode(PIN_DT3, OUTPUT);
    // SetMode(PIN_DT4, OUTPUT);
    // SetMode(PIN_DT5, OUTPUT);
    // SetMode(PIN_DT6, OUTPUT);
    // SetMode(PIN_DT7, OUTPUT);
    // SetMode(PIN_DP, OUTPUT);
    // SetMode(PIN_IO, OUTPUT);
    // SetMode(PIN_TAD, OUTPUT);
    // SetMode(PIN_IND, OUTPUT);
    //     PullConfig(PIN_DT0, GPIO_PULLNONE);
    // PullConfig(PIN_DT1, GPIO_PULLNONE);
    // PullConfig(PIN_DT2, GPIO_PULLNONE);
    // PullConfig(PIN_DT3, GPIO_PULLNONE);
    // PullConfig(PIN_DT4, GPIO_PULLNONE);
    // PullConfig(PIN_DT5, GPIO_PULLNONE);
    // PullConfig(PIN_DT6, GPIO_PULLNONE);
    // PullConfig(PIN_DT7, GPIO_PULLNONE);
    // PullConfig(PIN_DP, GPIO_PULLNONE);
    // PullConfig(PIN_IO, GPIO_PULLNONE);
    //     PullConfig(PIN_TAD, GPIO_PULLNONE);
    //     PullConfig(PIN_IND, GPIO_PULLNONE);

}

void GPIOBUS_Allwinner::Cleanup()
{
    LOGTRACE("%s", __PRETTY_FUNCTION__);
   
#if defined(__x86_64__) || defined(__X86__)
	return;
#else
	int i;
	int pin;

	// Release SEL signal interrupt
#ifdef USE_SEL_EVENT_ENABLE
	close(selevreq.fd);
#endif // USE_SEL_EVENT_ENABLE

	// // Set control signals
	// PinSetSignal(PIN_ENB, FALSE);
	// PinSetSignal(PIN_ACT, FALSE);
	// PinSetSignal(PIN_TAD, FALSE);
	// PinSetSignal(PIN_IND, FALSE);
	// PinSetSignal(PIN_DTD, FALSE);
	// PinConfig(PIN_ACT, board_type::gpio_high_low_e::GPIO_INPUT);
	// PinConfig(PIN_TAD, board_type::gpio_high_low_e::GPIO_INPUT);
	// PinConfig(PIN_IND, board_type::gpio_high_low_e::GPIO_INPUT);
	// PinConfig(PIN_DTD, board_type::gpio_high_low_e::GPIO_INPUT);

	// // Initialize all signals
	// for (i = 0; SignalTable[i] >= 0; i++)
	// {
	// 	pin = SignalTable[i];
	// 	PinSetSignal(pin, FALSE);
	// 	PinConfig(pin, board_type::gpio_high_low_e::GPIO_INPUT);
	// 	PullConfig(pin, GPIO_PULLNONE);
	// }

	// Set drive strength back to 8mA
	DrvConfig(3);
#endif // ifdef __x86_64__ || __X86__
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
}

void GPIOBUS_Allwinner::Reset()
{
// #if defined(__x86_64__) || defined(__X86__)
    return;
// #else
//     int i;
//     int j;

//     // Turn off active signal
//     SetControl(PIN_ACT, ACT_OFF);

// 	// Set all signals to off
// 	for (i = 0;; i++) {
// 		j = SignalTable[i];
// 		if (j < 0) {
// 			break;
// 		}

//         SetSignal(j, board_type::gpio_high_low_e::GPIO_STATE_LOW);
//     }

// 	if (actmode == mode_e::TARGET) {
// 		// Target mode

//         // Set target signal to input
//         SetControl(PIN_TAD, TAD_IN);
//         SetMode(PIN_BSY, RASCSI_PIN_IN);
//         SetMode(PIN_MSG, RASCSI_PIN_IN);
//         SetMode(PIN_CD,  RASCSI_PIN_IN);
//         SetMode(PIN_REQ, RASCSI_PIN_IN);
//         SetMode(PIN_IO,  RASCSI_PIN_IN);

//         // Set the initiator signal to input
//         SetControl(PIN_IND, IND_IN);
//         SetMode(PIN_SEL, RASCSI_PIN_IN);
//         SetMode(PIN_ATN, RASCSI_PIN_IN);
//         SetMode(PIN_ACK, RASCSI_PIN_IN);
//         SetMode(PIN_RST, RASCSI_PIN_IN);

//         // Set data bus signals to input
//         SetControl(PIN_DTD, DTD_IN);
//         SetMode(PIN_DT0, RASCSI_PIN_IN);
//         SetMode(PIN_DT1, RASCSI_PIN_IN);
//         SetMode(PIN_DT2, RASCSI_PIN_IN);
//         SetMode(PIN_DT3, RASCSI_PIN_IN);
//         SetMode(PIN_DT4, RASCSI_PIN_IN);
//         SetMode(PIN_DT5, RASCSI_PIN_IN);
//         SetMode(PIN_DT6, RASCSI_PIN_IN);
//         SetMode(PIN_DT7, RASCSI_PIN_IN);
//         SetMode(PIN_DP,  RASCSI_PIN_IN);
// 	} else {
// 		// Initiator mode

//         // Set target signal to input
//         SetControl(PIN_TAD, TAD_IN);
//         SetMode(PIN_BSY, RASCSI_PIN_IN);
//         SetMode(PIN_MSG, RASCSI_PIN_IN);
//         SetMode(PIN_CD,  RASCSI_PIN_IN);
//         SetMode(PIN_REQ, RASCSI_PIN_IN);
//         SetMode(PIN_IO,  RASCSI_PIN_IN);

//         // Set the initiator signal to output
//         SetControl(PIN_IND, IND_OUT);
//         SetMode(PIN_SEL, RASCSI_PIN_OUT);
//         SetMode(PIN_ATN, RASCSI_PIN_OUT);
//         SetMode(PIN_ACK, RASCSI_PIN_OUT);
//         SetMode(PIN_RST, RASCSI_PIN_OUT);

//         // Set the data bus signals to output
//         SetControl(PIN_DTD, DTD_OUT);
//         SetMode(PIN_DT0, RASCSI_PIN_OUT);
//         SetMode(PIN_DT1, RASCSI_PIN_OUT);
//         SetMode(PIN_DT2, RASCSI_PIN_OUT);
//         SetMode(PIN_DT3, RASCSI_PIN_OUT);
//         SetMode(PIN_DT4, RASCSI_PIN_OUT);
//         SetMode(PIN_DT5, RASCSI_PIN_OUT);
//         SetMode(PIN_DT6, RASCSI_PIN_OUT);
//         SetMode(PIN_DT7, RASCSI_PIN_OUT);
//         SetMode(PIN_DP,  RASCSI_PIN_OUT);
//     }

//     // Initialize all signals
//     signals = 0;
// #endif // ifdef __x86_64__ || __X86__
}

BYTE GPIOBUS_Allwinner::GetDAT()
{
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
    // LOGDEBUG("0:%02X 1:%02X 2:%02X 3:%02X 4:%02X 5:%02X 6:%02X 7:%02X P:%02X", GetSignal(PIN_DT0), GetSignal(PIN_DT1),GetSignal(PIN_DT2),GetSignal(PIN_DT3),GetSignal(PIN_DT4),GetSignal(PIN_DT5),GetSignal(PIN_DT6),GetSignal(PIN_DT7),GetSignal(PIN_DP));
    // // TODO: This is crazy inefficient...
    // DWORD data =
    //     ((GetSignal(PIN_DT0) ? 0x01: 0x00)<< 0) |
    //     ((GetSignal(PIN_DT1) ? 0x01: 0x00)<< 1) |
    //     ((GetSignal(PIN_DT2) ? 0x01: 0x00)<< 2) |
    //     ((GetSignal(PIN_DT3) ? 0x01: 0x00)<< 3) |
    //     ((GetSignal(PIN_DT4) ? 0x01: 0x00)<< 0) |
    //     ((GetSignal(PIN_DT5) ? 0x01: 0x00)<< 5) |
    //     ((GetSignal(PIN_DT6) ? 0x01: 0x00)<< 6) |
    //     ((GetSignal(PIN_DT7) ? 0x01: 0x00)<< 7);

    // return (BYTE)data;
    return 0;
}

void GPIOBUS_Allwinner::SetDAT(BYTE dat)
{
    // TODO: This is crazy inefficient...
    // SetSignal(PIN_DT0, dat & (1 << 0));
    // SetSignal(PIN_DT1, dat & (1 << 1));
    // SetSignal(PIN_DT2, dat & (1 << 2));
    // SetSignal(PIN_DT3, dat & (1 << 3));
    // SetSignal(PIN_DT4, dat & (1 << 4));
    // SetSignal(PIN_DT5, dat & (1 << 5));
    // SetSignal(PIN_DT6, dat & (1 << 6));
    // SetSignal(PIN_DT7, dat & (1 << 7));
        LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)

}

void GPIOBUS_Allwinner::MakeTable(void)
{
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
}

void GPIOBUS_Allwinner::SetControl(board_type::pi_physical_pin_e pin, board_type::gpio_high_low_e ast)
{
    // GPIOBUS_Allwinner::SetSignal(pin, ast);
        LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)

}

void GPIOBUS_Allwinner::SetMode(board_type::pi_physical_pin_e pin, board_type::gpio_direction_e mode)
{
    // LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
    // if(mode == board_type::gpio_high_low_e::GPIO_INPUT){
    //     pinMode(pin, INPUT);

    // }else{
    //     pinMode(pin, OUTPUT);
    // }
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
}

bool GPIOBUS_Allwinner::GetSignal(board_type::pi_physical_pin_e pin) const
{
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
    return false;
    // return (digitalRead(pin) != 0);
}

void GPIOBUS_Allwinner::SetSignal(board_type::pi_physical_pin_e pin, board_type::gpio_high_low_e ast)
{
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
    // digitalWrite(pin, ast);
}


//---------------------------------------------------------------------------
//
//	Wait for signal change
//
// TODO: maybe this should be moved to SCSI_Bus?
//---------------------------------------------------------------------------
bool GPIOBUS_Allwinner::WaitSignal(board_type::pi_physical_pin_e pin, board_type::gpio_high_low_e ast)
{
        LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)

// {
// 	// Get current time
// 	uint32_t now = SysTimer::instance().GetTimerLow();

// 	// Calculate timeout (3000ms)
// 	uint32_t timeout = 3000 * 1000;


// 	// end immediately if the signal has changed
// 	do {
// 		// Immediately upon receiving a reset
// 		Acquire();
// 		if (GetRST()) {
// 			return false;
// 		}

// 		// Check for the signal edge
//         if (((signals >> pin) ^ ~ast) & 1) {
// 			return true;
// 		}
// 	} while ((SysTimer::instance().GetTimerLow() - now) < timeout);

	// We timed out waiting for the signal
	return false;
}


void GPIOBUS_Allwinner::DisableIRQ()
{
    LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)
}

void GPIOBUS_Allwinner::EnableIRQ()
{
    LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)
}

void GPIOBUS_Allwinner::PinConfig(board_type::pi_physical_pin_e pin, board_type::gpio_direction_e mode)
{
        LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)

    // if(mode == board_type::gpio_high_low_e::GPIO_INPUT){
    //     pinMode(pin, INPUT);

    // }else{
    //     pinMode(pin, OUTPUT);
    // }
}


void GPIOBUS_Allwinner::PullConfig(board_type::pi_physical_pin_e pin, board_type::gpio_pull_up_down_e mode)
{

        // switch (mode)
		// {
		// case GPIO_PULLNONE:
        //     pullUpDnControl(pin,PUD_OFF);
		// 	break;
		// case GPIO_PULLUP:
        //     pullUpDnControl(pin,PUD_UP);
		// 		break;
		// case GPIO_PULLDOWN:
		// 	pullUpDnControl(pin,PUD_DOWN);
		// 	break;
		// default:
        //     LOGERROR("%s INVALID PIN MODE", __PRETTY_FUNCTION__);
		// 	return;
		// }
    LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)
}


void GPIOBUS_Allwinner::PinSetSignal(board_type::pi_physical_pin_e pin, board_type::gpio_high_low_e ast)
{
    // digitalWrite(pin, ast);
    LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)
}

void GPIOBUS_Allwinner::DrvConfig(DWORD drive)
{
    (void)drive;
    LOGERROR("%s not implemented!!", __PRETTY_FUNCTION__)
}

uint32_t GPIOBUS_Allwinner::Acquire()
{
    LOGWARN("%s NOT IMPLEMENTED", __PRETTY_FUNCTION__)
    // Only used for development/debugging purposes. Isn't really applicable
    // to any real-world RaSCSI application
    return 0;
}

#pragma GCC diagnostic pop




uint32_t GPIOBUS_Allwinner::sunxi_readl(volatile uint32_t *addr)
{
    #ifndef __arm__
    (void)addr;
    return 0;
    #else
    printf("sunxi_readl\n");
    uint32_t val = 0;
    uint32_t mmap_base = (uint32_t)addr & (~MAP_MASK);
    uint32_t mmap_seek = ((uint32_t)addr - mmap_base) >> 2;
    val = *(gpio_map + mmap_seek);
    return val;
    #endif
}   

void GPIOBUS_Allwinner::sunxi_writel(volatile uint32_t *addr, uint32_t val)
{
    #ifndef __arm__
    (void)addr;
    (void)val;
    return;
    #else
    printf("sunxi_writel\n");
    uint32_t mmap_base = (uint32_t)addr & (~MAP_MASK);
    uint32_t mmap_seek =( (uint32_t)addr - mmap_base) >> 2;
    *(gpio_map + mmap_seek) = val;
    #endif
}

int GPIOBUS_Allwinner::sunxi_setup(void)
{
    #ifndef __arm__
    return SETUP_MMAP_FAIL;
    #else
    int mem_fd;
    uint8_t *gpio_mem;
    // uint8_t *r_gpio_mem;
    // uint32_t peri_base;
    // uint32_t gpio_base;
    // unsigned char buf[4];
    // FILE *fp;
    // char buffer[1024];
    // char hardware[1024];
    // int found = 0;
	printf("enter to sunxi_setup\n");

    // mmap the GPIO memory registers
    if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0)
        return SETUP_DEVMEM_FAIL;

    if ((gpio_mem = (uint8_t*)malloc(BLOCK_SIZE + (PAGE_SIZE-1))) == NULL)
        return SETUP_MALLOC_FAIL;

    if ((uint32_t)gpio_mem % PAGE_SIZE)
        gpio_mem += PAGE_SIZE - ((uint32_t)gpio_mem % PAGE_SIZE);

    gpio_map = (uint32_t *)mmap( (caddr_t)gpio_mem, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, mem_fd, SUNXI_GPIO_BASE);
    pio_map = gpio_map + (SUNXI_GPIO_REG_OFFSET>>2);
//printf("gpio_mem[%x] gpio_map[%x] pio_map[%x]\n", gpio_mem, gpio_map, pio_map);
//R_PIO GPIO LMN
    r_gpio_map = (uint32_t *)mmap( (caddr_t)0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, SUNXI_R_GPIO_BASE);
    r_pio_map = r_gpio_map + (SUNXI_R_GPIO_REG_OFFSET>>2);
//printf("r_gpio_map[%x] r_pio_map[%x]\n", r_gpio_map, r_pio_map);

    if ((int32_t)gpio_map < 0)
        return SETUP_MMAP_FAIL;

    return SETUP_OK;
    #endif
}

void GPIOBUS_Allwinner::sunxi_set_pullupdn(int gpio, int pud)
{
    #ifndef __arm__
    (void)gpio;
    (void)pud;
    return;
    #else
    uint32_t regval = 0;
    int bank = GPIO_BANK(gpio); //gpio >> 5
    int index = GPIO_PUL_INDEX(gpio); // (gpio & 0x1f) >> 4
    int offset = GPIO_PUL_OFFSET(gpio); // (gpio) & 0x0F) << 1
	printf("sunxi_set_pullupdn\n");

    sunxi_gpio_t *pio = &((sunxi_gpio_reg_t *) pio_map)->gpio_bank[bank];
/* DK, for PL and PM */
    if(bank >= 11) {
      bank -= 11;
      pio = &((sunxi_gpio_reg_t *) r_pio_map)->gpio_bank[bank];
    }

    regval = *(&pio->PULL[0] + index);
    regval &= ~(3 << offset);
    regval |= pud << offset;
    *(&pio->PULL[0] + index) = regval;
    #endif
}

void GPIOBUS_Allwinner::sunxi_setup_gpio(int gpio, int direction, int pud)
{
    #ifndef __arm__
    (void)gpio;
    (void)direction;
    (void)pud;
    return;
    #else
    uint32_t regval = 0;
    int bank = GPIO_BANK(gpio); //gpio >> 5
    int index = GPIO_CFG_INDEX(gpio); // (gpio & 0x1F) >> 3
    int offset = GPIO_CFG_OFFSET(gpio); // ((gpio & 0x1F) & 0x7) << 2
    printf("sunxi_setup_gpio\n");
    sunxi_gpio_t *pio = &((sunxi_gpio_reg_t *) pio_map)->gpio_bank[bank];
/* DK, for PL and PM */
    if(bank >= 11) {
      bank -= 11;
      pio = &((sunxi_gpio_reg_t *) r_pio_map)->gpio_bank[bank];
    }

    set_pullupdn(gpio, pud);

    regval = *(&pio->CFG[0] + index);
    regval &= ~(0x7 << offset); // 0xf?
    if (INPUT == direction) {
        *(&pio->CFG[0] + index) = regval;
    } else if (OUTPUT == direction) {
        regval |=  (1 << offset);
        *(&pio->CFG[0] + index) = regval;
    } else {
        printf("line:%dgpio number error\n",__LINE__);
    }
    #endif
}

// Contribution by Eric Ptak <trouch@trouch.com>
int GPIOBUS_Allwinner::sunxi_gpio_function(int gpio)
{
    uint32_t regval = 0;
    int bank = GPIO_BANK(gpio); //gpio >> 5
    int index = GPIO_CFG_INDEX(gpio); // (gpio & 0x1F) >> 3
    int offset = GPIO_CFG_OFFSET(gpio); // ((gpio & 0x1F) & 0x7) << 2
     printf("sunxi_gpio_function\n");
    sunxi_gpio_t *pio = &((sunxi_gpio_reg_t *) pio_map)->gpio_bank[bank];
/* DK, for PL and PM */
    if(bank >= 11) {
      bank -= 11;
      pio = &((sunxi_gpio_reg_t *) r_pio_map)->gpio_bank[bank];
    }

    regval = *(&pio->CFG[0] + index);
    regval >>= offset;
    regval &= 7;
    return regval; // 0=input, 1=output, 4=alt0
}

void GPIOBUS_Allwinner::sunxi_output_gpio(int gpio, int value)
{
    int bank = GPIO_BANK(gpio); //gpio >> 5
    int num = GPIO_NUM(gpio); // gpio & 0x1F

 printf("gpio(%d) bank(%d) num(%d)\n", gpio, bank, num);
    sunxi_gpio_t *pio = &((sunxi_gpio_reg_t *) pio_map)->gpio_bank[bank];
/* DK, for PL and PM */
    if(bank >= 11) {
      bank -= 11;
      pio = &((sunxi_gpio_reg_t *) r_pio_map)->gpio_bank[bank];
    }

    if (value == 0)
        *(&pio->DAT) &= ~(1 << num);
    else
        *(&pio->DAT) |= (1 << num);
}

int GPIOBUS_Allwinner::sunxi_input_gpio(int gpio)
{
    uint32_t regval = 0;
    int bank = GPIO_BANK(gpio); //gpio >> 5
    int num = GPIO_NUM(gpio); // gpio & 0x1F

 printf("gpio(%d) bank(%d) num(%d)\n", gpio, bank, num);
    sunxi_gpio_t *pio = &((sunxi_gpio_reg_t *) pio_map)->gpio_bank[bank];
/* DK, for PL and PM */
    if(bank >= 11) {
      bank -= 11;
      pio = &((sunxi_gpio_reg_t *) r_pio_map)->gpio_bank[bank];
    }

    regval = *(&pio->DAT);
    regval = regval >> num;
    regval &= 1;
    return regval;
}

int GPIOBUS_Allwinner::bpi_piGpioLayout (void)
{
  FILE *bpiFd ;
  char buffer[1024];
  char hardware[1024];
  struct BPIBoards *board = nullptr;
  static int  gpioLayout = -1 ;

  if (gpioLayout != -1)	// No point checking twice
    return gpioLayout ;

  bpi_found = 0; // -1: not init, 0: init but not found, 1: found
  if ((bpiFd = fopen("/var/lib/bananapi/board.sh", "r")) == NULL) {
    return -1;
  }
  while(!feof(bpiFd)) {
    // TODO: check the output of fgets()
    char *ret = fgets(buffer, sizeof(buffer), bpiFd);
    (void)ret;
    sscanf(buffer, "BOARD=%s", hardware);
    LOGDEBUG("BPI: buffer[%s] hardware[%s]\n",buffer, hardware);

    //Search for board:
    for (board = bpiboard ; board->name != NULL ; ++board) {
      LOGDEBUG("BPI: name[%s] hardware[%s]\n",board->name, hardware);
      if (strcmp (board->name, hardware) == 0) {
        //gpioLayout = board->gpioLayout;
        gpioLayout = board->model; // BPI: use model to replace gpioLayout
        LOGDEBUG("BPI: name[%s] gpioLayout(%d)\n",board->name, gpioLayout);
        if(gpioLayout >= 21) {
          bpi_found = 1;
          break;
        }
      }
    }
    if(bpi_found == 1) {
      break;
    }
  }
  fclose(bpiFd);
  LOGDEBUG("BPI: name[%s] gpioLayout(%d)\n",board->name, gpioLayout);
  return gpioLayout ;
}

// int GPIOBUS_Allwinner::bpi_get_rpi_info(rpi_info *info)
// {
//   struct BPIBoards *board=bpiboard;
//   static int  gpioLayout = -1 ;
//   char ram[64];
//   char manufacturer[64];
//   char processor[64];
//   char type[64];

//   gpioLayout = bpi_piGpioLayout () ;
//   printf("BPI: gpioLayout(%d)\n", gpioLayout);
//   if(bpi_found == 1) {
//     board = &bpiboard[gpioLayout];
//     printf("BPI: name[%s] gpioLayout(%d)\n",board->name, gpioLayout);
//     sprintf(ram, "%dMB", piMemorySize [board->mem]);
//     sprintf(type, "%s", piModelNames [board->model]);
//      //add by jackzeng
//      //jude mtk platform
//     if(strcmp(board->name, "bpi-r2") == 0){
//         bpi_found_mtk = 1;
// 	printf("found mtk board\n");
//     }
//     sprintf(manufacturer, "%s", piMakerNames [board->maker]);
//     info->p1_revision = 3;
//     info->type = type;
//     info->ram  = ram;
//     info->manufacturer = manufacturer;
//     if(bpi_found_mtk == 1){
//         info->processor = "MTK";
//     }else{
// 	info->processor = "Allwinner";
//     }
    
//     strcpy(info->revision, "4001");
// //    pin_to_gpio =  board->physToGpio ;
//     pinToGpio_BP =  board->pinToGpio ;
//     physToGpio_BP = board->physToGpio ;
//     pinTobcm_BP = board->pinTobcm ;
//     //printf("BPI: name[%s] bType(%d) model(%d)\n",board->name, bType, board->model);
//     return 0;
//   }
//   return -1;
// }


void GPIOBUS_Allwinner::set_pullupdn(int gpio, int pud)
{
    int clk_offset = PULLUPDNCLK_OFFSET + (gpio/32);
    int shift = (gpio%32);

#ifdef BPI
    if( bpi_found == 1 ) {
        gpio = *(pinTobcm_BP + gpio);
        return sunxi_set_pullupdn(gpio, pud);
    }
#endif
    if (pud == PUD_DOWN)
        *(gpio_map+PULLUPDN_OFFSET) = (*(gpio_map+PULLUPDN_OFFSET) & ~3) | PUD_DOWN;
    else if (pud == PUD_UP)
        *(gpio_map+PULLUPDN_OFFSET) = (*(gpio_map+PULLUPDN_OFFSET) & ~3) | PUD_UP;
    else  // pud == PUD_OFF
        *(gpio_map+PULLUPDN_OFFSET) &= ~3;

    short_wait();
    *(gpio_map+clk_offset) = 1 << shift;
    short_wait();
    *(gpio_map+PULLUPDN_OFFSET) &= ~3;
    *(gpio_map+clk_offset) = 0;
}

void GPIOBUS_Allwinner::short_wait(void)
{
    int i;

    for (i=0; i<150; i++) {    // wait 150 cycles
        asm volatile("nop");
    }
}