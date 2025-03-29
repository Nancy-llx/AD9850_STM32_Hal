/*
*********************************************************************************************************
*
*	模块名称 : AD9850驱动模块
*	文件名称 : ad9850.c
*	版    本 : V1.0
*	说    明 : 驱动DDS芯片AD9850，产生正弦波
*	修改记录 : 2025/3/29 
* 
*
*********************************************************************************************************
*/


#include "ad9850.h"
//默认为串行模式，并行模式还没写
//#define AD9850_SERIAL_EN	/* 定义此行 表示串行模式；不定义此行 表示并口模式 */

#define AD9850_OSC_FREQ		125000000u	/* AD9850 模块外接的晶振频率, 单位Hz */

/*
	AD9850模块和STM32开发板连接方式：串行方式(只需要6根线)

   【AD9850模块排针】 【定义标签（STM32口线）】
        VCC ----------- 3.3V
        GND ----------- GND
      RESET ----------- RESET  (PA3)
        CLK ----------- CLK   (PA4)
      FQ_UD ----------- FQ_UD  (PA5)
         D7 ----------- D7  (PA6)
*/

//ps：此处硬件上需要在初始化前将 AD9850的2脚（D2)接GND,3脚(D1)和4脚(D0)接VCC；以进入串行模式

/* 定义IO端口 （如更改为其他IO, 只需修改下面4段代码即可 */
#define DDS_RESET_PORT	RESET_GPIO_Port
#define DDS_RESET_PIN	RESET_Pin

#define DDS_CLK_PORT	CLK_GPIO_Port
#define DDS_CLK_PIN	CLK_Pin

#define DDS_FQ_UD_PORT	FQ_UD_GPIO_Port
#define DDS_FQ_UD_PIN	FQ_UD_Pin

#define DDS_D7_PORT		D7_GPIO_Port
#define DDS_D7_PIN		D7_Pin

/* 定义IO端口 End */

#define DDS_RESET_1()	HAL_GPIO_WritePin(DDS_RESET_PORT,DDS_RESET_PIN,GPIO_PIN_SET)
#define DDS_RESET_0()	HAL_GPIO_WritePin(DDS_RESET_PORT,DDS_RESET_PIN,GPIO_PIN_RESET)

#define DDS_CLK_1()	HAL_GPIO_WritePin(DDS_CLK_PORT,DDS_CLK_PIN,GPIO_PIN_SET)
#define DDS_CLK_0()	HAL_GPIO_WritePin(DDS_CLK_PORT,DDS_CLK_PIN,GPIO_PIN_RESET)

#define DDS_FQ_UD_1()	HAL_GPIO_WritePin(DDS_FQ_UD_PORT,DDS_FQ_UD_PIN,GPIO_PIN_SET)
#define DDS_FQ_UD_0()	HAL_GPIO_WritePin(DDS_FQ_UD_PORT,DDS_FQ_UD_PIN,GPIO_PIN_RESET)

#define DDS_D7_1() HAL_GPIO_WritePin(DDS_D7_PORT,DDS_D7_PIN,GPIO_PIN_SET)
#define DDS_D7_0() HAL_GPIO_WritePin(DDS_D7_PORT,DDS_D7_PIN,GPIO_PIN_RESET)

static void AD9850_ResetToSerial(void);

/*
*********************************************************************************************************
*	函 数 名: AD9850_Init
*	功能说明: 初始化AD9850所连接的GPIO端口,此处默认初始化为串口模式
*	形    参：无
*	返 回 值: 无
*********************************************************************************************************
*/
void AD9850_Init(void)
{
	DDS_RESET_0();
	DDS_CLK_0();
	DDS_FQ_UD_0();
	DDS_D7_0();
  AD9850_ResetToSerial();
}

/*
*********************************************************************************************************
*	函 数 名: AD9850_ResetToSerial
*	功能说明: 复位AD9850，之后为串口模式
*	形    参：无
*	返 回 值: 无
*********************************************************************************************************
*/
static void AD9850_ResetToSerial(void)
{
	/* AD9850 进入串口模式的条件：见ad9850pdfP12
  
		首先硬件上需要将 AD9850 的2脚（D2)接GND,3脚(D1)和4脚(D0)接VCC；然后产生1个CLK脉冲和一个FQ_UP脉冲

		如需进入并口模式，软件可以在复位AD9850之前，将D2,D1,D0设置为全0即可。
  
    建议参考安富莱AD9850模块，在D2接下拉电阻；D1和D0接上拉电阻，此时只要保证D2、D1、D0排针处于悬空状态即可进入串口模式。
	*/

	DDS_CLK_0();
	DDS_FQ_UD_0();
	DDS_RESET_0();

	/* 产生一个复位脉冲 */
	DDS_RESET_1();
	DDS_RESET_0();

	/* 产生一个W_CLK脉冲 */
	DDS_CLK_1();
	DDS_CLK_0();

	/* 产生一个FQ_UP脉冲 */
	DDS_FQ_UD_1();
	DDS_FQ_UD_0();

	/* 之后，AD9850 进入串口模式 */
}

/*
*********************************************************************************************************
*	函 数 名: AD9850_WriteCmd
*	功能说明: 按串口协议，发送40bit控制命令，前32位用于频率控制,5位用于相位控制。1位用于电源休眠（Power down）控制,2位用于选择工作方式
*           用于选择工作方式的两个控制位,无论并行还是串行都写成00,避免与工厂测试用的保留控制字冲突
*	形    参：_ucPhase ：相位参数(一般填0）；_dOutFreq ：频率参数(浮点数)，单位Hz，可以输入 0.01Hz
*	返 回 值: 无
* 备    注：为了实现低于1Hz的输出信号，本程序使用了浮点数，因此请保证程序的堆栈空间足够。
            如果不需要处理很低的频率，可以将AD9850相关的函数修改为整数形参。

*********************************************************************************************************
*/
void AD9850_WriteCmd(uint8_t _ucPhase, double _dOutFreq)
{
	uint32_t ulFreqWord;
	uint8_t i;

	/* 输出信号频率计算公式：
		fOUT = (Δ Phase × CLKIN) / (2的32次方))
			 Δ Phase = value of 32-bit tuning word
			CLKIN     = input reference clock frequency in MHz
			fOUT      = frequency of the output signal in MHz
		换算为：
		Δ Phase = (fOUT * （2的32次方) / CLKIN
				 = (fOUT * 4294967296) / CLKIN
	*/

	/* AD9850 串行数据格式

		发送40个W_CLK，传送40个Bit （W0~W39)，也就是5个字节

		前4个字节（共32bit，低位先传）就是 Freq
	*/


	/* 根据波形频率计算32位的频率参数 */
	ulFreqWord =  (uint32_t)((_dOutFreq * 4294967295u) / AD9850_OSC_FREQ);

	/* 写32bit 频率字 */
	for (i = 0; i < 32; i++)
	{
		if (ulFreqWord & 0x00000001)
		{
			DDS_D7_1();
		}
		else
		{
			DDS_D7_0();
		}
		ulFreqWord >>= 1;
		DDS_CLK_1();
		DDS_CLK_0();
	}

	/* 发送5位控制字；相位参数。 一般填 0 */
	for (i = 0; i < 5; i++)
	{
		if (_ucPhase & 0x00000001)
		{
			DDS_D7_1();
		}
		else
		{
			DDS_D7_0();
		}
		_ucPhase >>= 1;
		DDS_CLK_1();
		DDS_CLK_0();
	}
  
  /* 发送3位默认设置，均为0 */
  for (i = 0; i < 3; i++)
  {
    DDS_D7_0();
    DDS_CLK_1();
		DDS_CLK_0();
  }

	/* 之前的命令仅写到AD9850的缓冲区；现在给一个FQ_UD脉冲，将更新寄存器影响输出 */
	DDS_FQ_UD_1();
	DDS_FQ_UD_0();
}
