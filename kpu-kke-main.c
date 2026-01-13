/*
 * File:   kpu-kke-aht30.c
 * Author: R�bert Fodor
 * Device: PIC16F628A
 * Sensor: AHT30
 */

// PIC16F628A Configuration Bit Settings
#pragma config FOSC = INTOSCIO  // Internal Oscillator (RA6/RA7 are I/O)
#pragma config WDTE = OFF       // Watchdog Timer Disabled
#pragma config PWRTE = ON       // Power-up Timer Enabled
#pragma config MCLRE = OFF      // RA5 is Input
#pragma config BOREN = OFF      // Brown-out Detect Disabled
#pragma config LVP = OFF        // Low-Voltage Programming Disabled
#pragma config CPD = OFF        // Data Code Protection Off
#pragma config CP = OFF         // Code Protection Off

#include <xc.h>

#define _XTAL_FREQ 4000000

#define RELAY_HUM_PIN   RB3
#define RELAY_HUM_DIR   TRISB3
#define RELAY_TEMP_PIN  RB0
#define RELAY_TEMP_DIR  TRISB0
#define SDA_PIN     RB4
#define SDA_DIR     TRISB4
#define SCL_PIN     RB5
#define SCL_DIR     TRISB5
#define AHT30_W_ADDR 0x70
#define AHT30_R_ADDR 0x71

unsigned char stored_key = 0x67;
unsigned int hum_limit_low = 800;
unsigned int hum_limit_high = 900;
unsigned int temp_limit_low = 270;
unsigned int temp_limit_high = 300;

void I2C_Idle(void)
{
    SDA_DIR = 1; SCL_DIR = 1;
}

void I2C_Start(void)
{
    SDA_DIR = 1; SCL_DIR = 1; __delay_us(5);
    SDA_DIR = 0; SDA_PIN = 0; __delay_us(5);
    SCL_DIR = 0; SCL_PIN = 0;
}

void I2C_Stop(void)
{
    SCL_DIR = 0; SCL_PIN = 0; SDA_DIR = 0; SDA_PIN = 0; __delay_us(5);
    SCL_DIR = 1; __delay_us(5); SDA_DIR = 1;
}

unsigned char I2C_Write(unsigned char data)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        if ((data & 0x80) == 0)
        {
            SDA_DIR = 0; SDA_PIN = 0;
        }
        else
        { 
            SDA_DIR = 1;
        }
        __delay_us(2); SCL_DIR = 1; __delay_us(5); SCL_DIR = 0; SCL_PIN = 0;
        data <<= 1;
    }
    SDA_DIR = 1; __delay_us(2); SCL_DIR = 1; __delay_us(2);
    unsigned char ack = !SDA_PIN;
    __delay_us(2); SCL_DIR = 0; SCL_PIN = 0;
    return ack;
}

unsigned char I2C_Read(unsigned char ack)
{
    unsigned char i, data = 0;
    SDA_DIR = 1;
    for (i = 0; i < 8; i++)
    {
        __delay_us(2); SCL_DIR = 1; __delay_us(2);
        data <<= 1;
        if (SDA_PIN) data |= 1;
        SCL_DIR = 0; SCL_PIN = 0;
    }
    if (ack) { SDA_DIR = 0; SDA_PIN = 0; }
    else     { SDA_DIR = 1; }
    __delay_us(2); SCL_DIR = 1; __delay_us(5); SCL_DIR = 0; SCL_PIN = 0; SDA_DIR = 1;
    return data;
}

void AHT30_Init(void)
{
    __delay_ms(100);
    
    I2C_Start();
    I2C_Write(AHT30_W_ADDR);
    I2C_Write(0xBE);
    I2C_Write(0x08);
    I2C_Write(0x00);
    I2C_Stop();
    __delay_ms(10);
}

unsigned char AHT30_Read(long *temp, long *hum)
{
    unsigned char data[6];
    
    I2C_Start();
    if (I2C_Write(AHT30_W_ADDR) == 0)
    {
        I2C_Stop();
        return 0;
    }
    I2C_Write(0xAC);
    I2C_Write(0x33);
    I2C_Write(0x00);
    I2C_Stop();
    
    __delay_ms(80);
    
    I2C_Start();
    I2C_Write(AHT30_R_ADDR);
    for(int i=0; i<6; i++)
    {
        data[i] = I2C_Read(i < 5);
    }
    I2C_Stop();
    
    if ((data[0] & 0x80) != 0)
    {
        return 0;
    }
    
    unsigned long raw_h = ((unsigned long)data[1] << 12) | ((unsigned long)data[2] << 4) | (data[3] >> 4);
    unsigned long raw_t = ((unsigned long)(data[3] & 0x0F) << 16) | ((unsigned long)data[4] << 8) | data[5];
    
    *hum = (raw_h * 1000) >> 20;
    *temp = ((raw_t * 2000) >> 20) - 500;
    
    return 1;
}

void UART_Init(void)
{
    SPBRG = 25; BRGH = 1;
    SYNC = 0;
    SPEN = 1;
    TXEN = 1;
    TRISB1 = 1; TRISB2 = 0;
}

void UART_Write(char data)
{
    while (!TRMT);
    TXREG = data;
}

void NumToStr(char* buf, unsigned int num)
{
    if (num > 9999)
    {
        num = 9999;
    }
    
    char d1 = '0'; char d2 = '0'; char d3 = '0'; char d4 = '0';
    while (num >= 1000)
    {
        num -= 1000;
        d1++;
    }
    while (num >= 100)
    {
        num -= 100;
        d2++;
    }
    while (num >= 10)
    {
        num -= 10;
        d3++;
    }
    d4 += (char)num;
    buf[0] = d1; buf[1] = d2; buf[2] = d3; buf[3] = d4;
}

void main(void)
{
    CMCON = 0x07;   
    PORTB = 0x00;
    TRISA = 0xFF;
    RELAY_HUM_DIR = 0;
    RELAY_TEMP_DIR = 0;
    
    UART_Init();
    I2C_Idle();
    AHT30_Init();

    unsigned int timer_counter = 0;
    unsigned int relay_hum_cooldown = 15;
    
    long temp_c = 0;
    long hum_p = 0;
    char buffer[9];
    
    unsigned char current_porta = 0xFF; 
    unsigned char last_porta = 0xFF;

    while (1)
    {   
        current_porta = PORTA;
        
        if ((last_porta & 0x01) == 0 && (current_porta & 0x01) != 0)
        {
            hum_limit_low = 800;
            hum_limit_high = 900;
            temp_limit_low = 270;
            temp_limit_high = 300;
            UART_Write('M');
            UART_Write('1');
            UART_Write('\r'); 
            UART_Write('\n');
        }
        
        if ((last_porta & 0x02) == 0 && (current_porta & 0x02) != 0)
        {
            hum_limit_low = 700;
            hum_limit_high = 800;
            temp_limit_low = 260;
            temp_limit_high = 290;
            UART_Write('M');
            UART_Write('2');
            UART_Write('\r'); 
            UART_Write('\n');
        }

        if ((last_porta & 0x40) == 0 && (current_porta & 0x40) != 0)
        {
            hum_limit_low = 600;
            hum_limit_high = 750;
            temp_limit_low = 250;
            temp_limit_high = 300;
            UART_Write('M');
            UART_Write('3');
            UART_Write('\r'); 
            UART_Write('\n');
        }

        if ((last_porta & 0x80) == 0 && (current_porta & 0x80) != 0)
        {
            hum_limit_low = 500;
            hum_limit_high = 650;
            temp_limit_low = 240;
            temp_limit_high = 300;
            UART_Write('M');
            UART_Write('4');
            UART_Write('\r'); 
            UART_Write('\n');
        }
        
        last_porta = current_porta;
        
        if ((timer_counter % 50) == 0) 
        {
            if (AHT30_Read(&temp_c, &hum_p))
            {
                if (hum_p < hum_limit_low)
                {
                    if (relay_hum_cooldown == 0) RELAY_HUM_PIN = 1;
                }
                else if (hum_p > hum_limit_high)
                {
                    if (RELAY_HUM_PIN == 1) 
                    {
                        RELAY_HUM_PIN = 0;
                        relay_hum_cooldown = 15;
                    }
                }
                
                if (temp_c < temp_limit_low)
                {
                     RELAY_TEMP_PIN = 1;
                }
                else if (temp_c >= temp_limit_high)
                {
                     RELAY_TEMP_PIN = 0;
                }
            }
        }
        
        if ((timer_counter % 100) == 0) 
        {
            NumToStr(&buffer[0], (unsigned int)temp_c);
            NumToStr(&buffer[4], (unsigned int)hum_p);
            
            for (int k=0; k < 8; k++)
            {
                UART_Write(buffer[k] ^ stored_key);
            }
            UART_Write('\r'); 
            UART_Write('\n');
        }

        __delay_ms(100); 
        timer_counter++;
        if (timer_counter >= 300) 
        {
            timer_counter = 0;
        }
        
        if ((timer_counter % 10) == 0) 
        {
            if (relay_hum_cooldown > 0)
            {
                relay_hum_cooldown--;
            }
        }
    }
}
