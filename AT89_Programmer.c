/**   AT89LP6440 Programmer v0.1
 *    Copyright (C) 2014 Joey Shepard
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <msp430.h>
#include <stdbool.h>
#include <string.h>

#define UART_RXD        BIT1  //P1.1 To TXD of slave
#define UART_TXD        BIT2  //P1.2 To RXD of slave

#define AT89_SS         BIT3  //P1.3 AT89 SS
#define AT89_CLOCK      BIT5  //P1.5 AT89 clock
#define AT89_MISO       BIT6  //P1.6 AT89 data out
#define AT89_MOSI       BIT7  //P1.7 AT89 data in
#define AT89_RST        BIT3  //P2.3 AT89 reset

#define LED             BIT2  //P2.2 LED

#define XOFF            0x13
#define XON             0x11

void UART_Send(unsigned char data);
void UART_Text(char *data);
unsigned char UART_Receive();
void UART_Hex(unsigned char data);
void ProgStart();
void SPI_Send(unsigned char data);
unsigned char SPI_Receive();
void ProgDelayStop();
void ProgStop();
unsigned char CmdEnable();
void CmdErase();
void CmdPollBusy();

void SPI_Text(unsigned char *data);

void delay_ms(int ms);

#define RINGBUFFERSIZE    64
#define SPIBUFFERSIZE     64
#define UARTRXBUFFERSIZE  64

volatile unsigned char UART_RingBuff[RINGBUFFERSIZE];
volatile int UART_RingCount;
volatile int UART_RingPtr;
volatile bool UART_RingReady;

volatile unsigned char UART_RxBuff[UARTRXBUFFERSIZE];
volatile int UART_RxCount;
volatile int UART_RxPtr;

volatile unsigned char SPI_RingBuff[SPIBUFFERSIZE];
volatile int SPI_RingCount;
volatile int SPI_RingPtr;
volatile bool SPI_RingReady;

volatile int SPI_ReceiveCount;
volatile int SPI_ReceiveReady;
volatile unsigned char SPI_ReceiveBuff;
volatile bool SPI_SendStop;

void main(void)
{
  WDTCTL=WDTPW + WDTHOLD;

  BCSCTL1=CALBC1_16MHZ;
  DCOCTL=CALDCO_16MHZ;

  UCA0CTL1=UCSWRST|UCSSEL_2;
  UCA0CTL0 = 0;
  //19.2k
  //UCA0MCTL = UCBRS_3+UCBRF_0;
  //UCA0BR0 = 0x41;
  //UCA0BR1 = 0x03;

  //57.6kk
  UCA0MCTL = UCBRS_6+UCBRF_0;
  UCA0BR0 = 0x15;
  UCA0BR1 = 0x01;
  UCA0CTL1&=~UCSWRST;

  UCB0CTL1=UCSWRST;
  UCB0CTL0=UCCKPH|UCMST|UCSYNC|UCMSB;//or UCCKPL?
  UCB0CTL1|=UCSSEL_2;
  UCB0BR0=4;//5mhz max so 4 should work
  UCB0BR1=0;
  UCB0CTL1&=~UCSWRST;

  UC0IE|=UCA0TXIE|UCA0RXIE|UCB0TXIE|UCB0RXIE;
  __enable_interrupt();

  P1OUT=AT89_SS;
  P1DIR=AT89_SS;

  P2OUT=LED+AT89_RST;
  P2DIR=LED+AT89_RST;

  P1SEL=AT89_CLOCK|AT89_MISO|AT89_MOSI|UART_RXD|UART_TXD;
  P1SEL2=AT89_CLOCK|AT89_MISO|AT89_MOSI|UART_RXD|UART_TXD;

  #define INBUFFLEN 50

  int inptr=0,i,j;
  unsigned char inbuffer[INBUFFLEN]={0};
  unsigned char inkey,oddbyte,crc;
  unsigned int address,offset,start_address;
  bool eof;

  while (1)
  {
    UC0IE&=~(UCA0TXIE|UCA0RXIE|UCB0TXIE|UCB0RXIE);
    UART_RingCount=0;
    UART_RingPtr=0;
    UART_RingReady=true;

    UART_RxCount=0;
    UART_RxPtr=0;

    SPI_RingCount=0;
    SPI_RingPtr=0;
    SPI_RingReady=true;
    SPI_ReceiveReady=0;
    SPI_ReceiveCount=0;
    SPI_SendStop=false;

    P1OUT|=AT89_SS;

    UC0IFG&=~(UCA0TXIFG|UCA0RXIFG|UCB0TXIFG|UCB0RXIFG);
    UC0IE|=UCA0TXIE|UCA0RXIFG|UCB0TXIE|UCB0RXIE;

    inptr=0;
    inbuffer[0]=0;

    P1SEL|=(AT89_CLOCK|AT89_MISO|AT89_MOSI);
    P1SEL2|=(AT89_CLOCK|AT89_MISO|AT89_MOSI);

    delay_ms(10);
    P2OUT|=AT89_RST;
    delay_ms(10);
    P2OUT&=~AT89_RST;
    delay_ms(10);

    UART_Text("\r\n\nAT89LP6440 PROGRAMMER v0.1\r\n>");
    UART_Send(XON);

    do
    {
      i=0;
      if (CmdEnable()!=0x53)
      {
        UART_Text("COULD NOT CONNECT. PRESS ANY KEY TO RECONNECT.\r\n");
        UART_Receive();
        i=1;
      }
    }while(i==1);

    do
    {
      inkey=UART_Receive();

      if (inkey==13)
      {
        if (inptr) UART_Text("\r\n");
        if (inptr==0)
        {
          //inkey=3;//reconnect when enter
          //or do nothing
        }
        else if (!strcmp(inbuffer,"E"))
        {
          //UART_Text("ERASING...");
          CmdErase();
          //UART_Text("DONE");
        }
        else if (!strcmp(inbuffer,"L"))
        {
          oddbyte=0;
          crc=0;
          inptr=0;
          eof=false;
          do
          {
            inkey=UART_Receive();
            if (inkey==3) break;
            if ((inkey>='a')&&(inkey<='f')) inkey-=32;
            if (((inkey>='0')&&(inkey<='9'))||((inkey>='A')&&(inkey<='F')))
            {
              if (inkey<='9') inkey-='0';
              else inkey-=55;

              if (oddbyte==0) inbuffer[inptr]=inkey*16;
              else
              {
                inbuffer[inptr]+=inkey;
                crc+=inbuffer[inptr];
                inptr++;
              }
              oddbyte=~oddbyte;

              if (!oddbyte)
              {
                if (inptr==3)
                {
                  address=inbuffer[1]*256+inbuffer[2];
                  start_address=address;
                  offset=4;
                }
                else if (inbuffer[0]==inptr-5)
                {
                  if (oddbyte) UART_Send('U');//Odd number of bytes!
                  else if (crc) UART_Send('C');//Bad checksum
                  else
                  {
                    UART_Send('.');
                    UART_Send(XOFF);
                    if (inbuffer[3]==1)
                    {
                      eof=true;
                      inkey=0;
                    }
                    else
                    {
                      if (!(P1OUT|AT89_SS))
                      {
                        UART_Text("\r\nSPI BUFFER OVERFLOW. REDUCE BAUD RATE.\r\n");
                        while (UART_Receive()!=3);
                        eof=true;
                      }
                      else
                      {
                        j=0;
                        ProgStart();
                        SPI_Send(0xAA);
                        SPI_Send(0x55);
                        SPI_Send(0x60);
                        SPI_Send('X');
                        SPI_Send('Y');

                        do
                        {
                          i=SPI_Receive();
                        } while (!(i&1));
                        ProgStop();
                        /*if (!(i&1))
                        {
                          UART_Text("\r\nFLASH BUSY. REDUCE BAUD RATE.\r\n");
                          while (UART_Receive()!=3);
                          eof=true;
                        }*/
                      }

                      if (eof==false)
                      {
                        ProgStart();
                        SPI_Send(0xAA);
                        SPI_Send(0x55);
                        SPI_Send(0x50);
                        SPI_Send(start_address>>8);
                        SPI_Send(start_address&0xFF);
                        for (i=offset;i<inptr-1;i++) SPI_Send(inbuffer[i]);
                        ProgDelayStop();
                      }
                    }
                  }
                  oddbyte=0;
                  crc=0;
                  inptr=0;
                  UART_Send(XON);
                }
                else if (inptr>4)
                {
                  address++;
                  if ((address&63)==0)
                  {
                    ProgStart();
                    SPI_Send(0xAA);
                    SPI_Send(0x55);
                    SPI_Send(0x60);
                    SPI_Send('X');
                    SPI_Send('Y');

                    do
                    {
                      i=SPI_Receive();
                    } while (!(i&1));
                    ProgStop();

                    /*if (!(i&1))
                    {
                      UART_Text("\r\nFLASH BUSY. REDUCE BAUD RATE.\r\n");
                      while (UART_Receive()!=3);
                      eof=true;
                    }*/

                    UART_Send(XOFF);
                    ProgStart();
                    SPI_Send(0xAA);
                    SPI_Send(0x55);
                    SPI_Send(0x50);
                    SPI_Send(start_address>>8);
                    SPI_Send(start_address&0xFF);
                    for (i=offset;i<inptr;i++) SPI_Send(inbuffer[i]);
                    ProgDelayStop();
                    start_address=address;
                    offset=inptr;
                    UART_Send(XON);
                  }
                }
              }
            }
          } while(!eof);
          if (inkey!=3) UART_Receive();
          UART_Text("\r\n");
        }
        else if (!strcmp(inbuffer,"V"))
        {
          oddbyte=0;
          crc=0;
          inptr=0;
          eof=false;
          do
          {
            inkey=UART_Receive();
            if (inkey==3) break;
            if ((inkey>='a')&&(inkey<='f')) inkey-=32;
            if (((inkey>='0')&&(inkey<='9'))||((inkey>='A')&&(inkey<='F')))
            {
              if (inkey<='9') inkey-='0';
              else inkey-=55;

              if (oddbyte==0) inbuffer[inptr]=inkey*16;
              else
              {
                inbuffer[inptr]+=inkey;
                inptr++;
              }
              oddbyte=~oddbyte;

              if (!oddbyte)
              {
                if ((inptr==4)&&(inbuffer[3]==0))
                {
                  address=inbuffer[1]*256+inbuffer[2];
                  offset=4;
                  UART_Send(XOFF);
                  ProgStart();
                  SPI_Send(0xAA);
                  SPI_Send(0x55);
                  SPI_Send(0x30);
                  SPI_Send(address>>8);
                  SPI_Send(address&0xFF);
                  UART_Send(XON);
                }
                else if (inbuffer[0]==inptr-5)
                {
                  if (inbuffer[3]==1)
                  {
                    eof=true;
                    inkey=0;
                  }
                  ProgStop();
                  oddbyte=0;
                  inptr=0;
                  address=0;
                  if (crc&2) UART_Send('x');
                  else UART_Send('.');
                  crc&=~2;
                }
                else if (inptr>4)
                {
                  i=SPI_Receive();
                  //UART_Hex(i);
                  if (i!=inbuffer[inptr-1])
                  {
                    crc=2|1;
                  }
                  address++;
                  if ((address&63)==0)
                  {
                    UART_Send(XOFF);
                    ProgStop();
                    ProgStart();
                    SPI_Send(0xAA);
                    SPI_Send(0x55);
                    SPI_Send(0x30);
                    SPI_Send(address>>8);
                    SPI_Send(address&0xFF);
                    UART_Send(XON);
                  }
                }
              }
            }
          } while(!eof);
          if (inkey!=3)
          {
            UART_Receive();
            if (crc==0) UART_Text("\r\nVERIFYING DONE\r\n");
            else UART_Text("\r\nVERIFYING FAILED\r\n");
          }
        }

        else if (!strcmp(inbuffer,"R"))
        {
          P1SEL&=~(AT89_CLOCK|AT89_MISO|AT89_MOSI);
          P1SEL2&=~(AT89_CLOCK|AT89_MISO|AT89_MOSI);
          P1DIR&=~(AT89_CLOCK|AT89_MISO|AT89_MOSI);

          P2OUT|=AT89_RST;
        }
        else if (!strcmp(inbuffer,"S"))
        {
          P1SEL|=(AT89_CLOCK|AT89_MISO|AT89_MOSI);
          P1SEL2|=(AT89_CLOCK|AT89_MISO|AT89_MOSI);

          P2OUT&=~AT89_RST;
          inkey=3;
        }
        /*else if (!strcmp(inbuffer,"T"))
        {
          UART_Text("WRITING...");
          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0x50);
          SPI_Send(0x00);
          SPI_Send(0x20);
          for (i=20;i<30;i++) SPI_Send(i);
          ProgStop();

          //delay_ms(5);
        }*/
        /*else if (!strcmp(inbuffer,"U"))
        {
          UART_Text("READING...");
          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0x30);
          SPI_Send(0x00);
          SPI_Send(0x7E);
          for (i=0;i<2;i++) UART_Hex(SPI_Receive());
          ProgStop();

          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0x30);
          SPI_Send(0x00);
          SPI_Send(0x80);
          for (i=0;i<2;i++) UART_Hex(SPI_Receive());
          ProgStop();
        }
        else if (!strcmp(inbuffer,"W"))
        {
          UART_Text("TESTING...");
          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0x30);
          SPI_Send(0x00);
          SPI_Send(0x00);
          for (i=0;i<32;i++) UART_Hex(SPI_Receive());
          ProgStop();
        }*/
        /*else if (!strcmp(inbuffer,"C"))
        {
          P2OUT|=AT89_RST;
          delay_ms(10);
          P2OUT&=~AT89_RST;
          delay_ms(10);

          UC0IE&=~(UCB0TXIE|UCB0RXIE);

          P1OUT|=AT89_SS;
          delay_ms(10);
          P1OUT&=~AT89_SS;
          delay_ms(10);

          UCB0TXBUF=0xAA;
          delay_ms(10);
          UCB0TXBUF=0x55;
          delay_ms(10);
          UCB0TXBUF=0xAC;
          delay_ms(10);
          UCB0TXBUF=0x53;
          delay_ms(10);
          UCB0TXBUF=0x00;
          delay_ms(10);
          i=UCB0RXBUF;
          if (i==0x53) UART_Text("DONE\r\n");
          else
          {
            UART_Text("FAIL: ");
            UART_Hex(i);
            UART_Text("\r\n");
          }
          P1OUT|=AT89_SS;
          delay_ms(10);

          UC0IE|=(UCB0TXIE|UCB0RXIE);
        }*/
        /*else if (!strcmp(inbuffer,"V"))
        {
          UART_Text("ERASING...");

          UC0IE&=~(UCB0TXIE|UCB0RXIE);

          P1OUT|=AT89_SS;
          delay_ms(10);
          P1OUT&=~AT89_SS;
          delay_ms(10);

          UCB0TXBUF=0xAA;
          delay_ms(10);
          UCB0TXBUF=0x55;
          delay_ms(10);
          UCB0TXBUF=0x8A;
          delay_ms(10);
          P1OUT|=AT89_SS;
          delay_ms(10);
          UC0IFG&=~UCB0RXIFG;

          UC0IE|=(UCB0TXIE|UCB0RXIE);
        }*/
        else if (!strcmp(inbuffer,"C"))
        {
          UART_Text("FUSES: ");
          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0x61);
          SPI_Send(0x00);
          SPI_Send(0x00);
          for (i=0;i<12;i++)
          {
            UART_Hex(SPI_Receive());
            UART_Send(' ');
          }
          ProgStop();
          UART_Text("\r\n");
        }
        else if (inbuffer[0]=='F')
        {
          if (inptr>13) UART_Text("TOO MANY FUSES. MAX IS 12.");
          else
          {
            eof=false;
            ProgStart();
            SPI_Send(0xAA);
            SPI_Send(0x55);
            SPI_Send(0x61);
            SPI_Send(0x00);
            SPI_Send(0x00);
            for (i=0;i<12;i++)
            {
              if (((i+2)>inptr)||(inbuffer[i+1]=='X')) inbuffer[i]=SPI_Receive();
              else
              {
                SPI_Receive();
                if (inbuffer[i+1]=='1') inbuffer[i]=0xFF;
                else if (inbuffer[i+1]=='0') inbuffer[i]=0;
                else
                {
                  eof=true;
                  UART_Text("VALID FLAGS ARE 1, 0, and X.");
                  UART_Hex(i);
                  UART_Hex(inptr);
                  break;
                }
              }
            }
            ProgStop();

            if (!eof)
            {
              ProgStart();
              SPI_Send(0xAA);
              SPI_Send(0x55);
              SPI_Send(0xF1);
              SPI_Send(0x00);
              SPI_Send(0x00);

              for (i=0;i<12;i++) SPI_Send(inbuffer[i]);
              ProgStop();
              UART_Text("FUSES SET.\r\n");
            }
          }
        }
        /*else if (!strcmp(inbuffer,"H"))
        {
          UART_Text("LOCK BITS: ");
          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0x64);
          SPI_Send(0x00);
          SPI_Send(0x00);
          UART_Hex(SPI_Receive());
          UART_Send(' ');
          UART_Hex(SPI_Receive());
          ProgStop();
        }*/
        /*else if (!strcmp(inbuffer,"J"))
        {
          UART_Text("WRITING LOCK BITS");
          ProgStart();
          SPI_Send(0xAA);
          SPI_Send(0x55);
          SPI_Send(0xE4);
          SPI_Send(0x00);
          SPI_Send(0x00);
          SPI_Send(0xFF);
          SPI_Send(0xFF);
          UART_Hex(SPI_Receive());
          UART_Send(' ');
          UART_Hex(SPI_Receive());
          ProgStop();
        }*/
        else
        {
          UART_Text("UNKOWN COMMAND");
        }

        if ((inkey==13)&&(inptr==0))
        {
          UART_Text("\r\n>");
        }
        else if (inkey!=3)
        {
          UART_Text("\r\n>");
          inptr=0;
          inbuffer[0]=0;
        }
      }

      if ((inkey>='a')&&(inkey<='z')) inkey-=32;
      if (inptr<INBUFFLEN)
      {
        if (((inkey>='A')&&(inkey<='Z'))||((inkey>='0')&&(inkey<='9')))
        {
          inbuffer[inptr++]=inkey;
          inbuffer[inptr]=0;
          UART_Send(inkey);
        }
      }
      if (inptr>0)
      {
        if (inkey==8)//Backspace
        {
          inptr--;
          inbuffer[inptr]=0;
          UART_Send(8);
          UART_Send(' ');
          UART_Send(8);
        }
      }
    } while(inkey!=3);//Ctrl-C
  }
}

__attribute__((interrupt(USCIAB0TX_VECTOR))) static void USCI0TX_ISR(void)
{
  int i;
  if (UC0IFG&UCA0TXIFG)
  {
    if (UART_RingCount)
    {
      i=UART_RingPtr-UART_RingCount;
      if (i<0) i+=RINGBUFFERSIZE;
      UCA0TXBUF=UART_RingBuff[i];
      UART_RingCount--;
      UART_RingReady=false;
    }
    else UART_RingReady=true;
    UC0IFG &= ~UCA0TXIFG;
  }

  if ((UC0IFG&UCB0TXIFG)&&(UC0IE&UCB0TXIE))
  {
    if (SPI_RingCount)
    {
      i=SPI_RingPtr-SPI_RingCount;
      if (i<0) i+=SPIBUFFERSIZE;
      UCB0TXBUF=SPI_RingBuff[i];
      SPI_RingCount--;
      SPI_RingReady=false;
    }
    else
    {
      SPI_RingReady=true;
      UC0IFG &= ~UCB0TXIFG;
    }
  }
}

__attribute__((interrupt(USCIAB0RX_VECTOR))) static void USCI0RX_ISR(void)
{
  if ((UC0IFG&UCA0RXIFG)&&(UC0IE&UCA0RXIE))
  {
    UART_RxBuff[UART_RxPtr]=UCA0RXBUF;
    UART_RxPtr++;
    if (UART_RxPtr==UARTRXBUFFERSIZE) UART_RxPtr=0;
    UART_RxCount++;
    if (UART_RxCount==UARTRXBUFFERSIZE) UART_Text("\r\nRX RING OVERFLOW.\r\n");
    if (UCA0STAT & UCOE) UART_Text("\r\nRX BUFFER OVERFLOW.\r\n");;
  }

  if ((UC0IFG&UCB0RXIFG)&&(UC0IE&UCB0RXIE))//maybe remove these to save time
  {
    SPI_ReceiveCount--;
    if (SPI_ReceiveCount==0)
    {
      SPI_ReceiveBuff=UCB0RXBUF;
      if (SPI_SendStop)
      {
        //while (P2IN&1);//This is untested. Blocking here could be bad.
        P1OUT|=AT89_SS;
        SPI_SendStop=false;
      }
    }
    UC0IFG&=~UCB0RXIFG;
  }
}

void UART_Send(unsigned char data)
{
  UC0IE&=~UCA0TXIE;
  if (UART_RingReady)
  {
    UCA0TXBUF=data;
    UART_RingReady=false;
  }
  else
  {
    UART_RingBuff[UART_RingPtr]=data;
    UART_RingPtr++;
    if (UART_RingPtr==RINGBUFFERSIZE) UART_RingPtr=0;
    UART_RingCount++;
    UC0IE|=UCA0TXIE;
    while(UART_RingCount==RINGBUFFERSIZE);
  }
  UC0IE|=UCA0TXIE;
}

void UART_Text(char *data)
{
  int i=0;
  while (data[i]) UART_Send(data[i++]);
}

/*unsigned char UART_Receive()
{
  while (!(UC0IFG&UCA0RXIFG));
  return UCA0RXBUF;
}*/

unsigned char UART_Receive()
{
  int i, buff;
  while (!UART_RxCount);
  UC0IE&=~UCA0RXIE;
  i=UART_RxPtr-UART_RxCount;
  if (i<0) i+=UARTRXBUFFERSIZE;
  buff=UART_RxBuff[i];
  UART_RxCount--;
  UC0IE|=UCA0RXIE;
  return buff;
}

void UART_Hex(unsigned char data)
{
  unsigned char buff;
  buff=data/16;
  if (buff>9) buff+=55;
  else buff+='0';
  UART_Send(buff);
  buff=data%16;
  if (buff>9) buff+=55;
  else buff+='0';
  UART_Send(buff);
}

void ProgStart()
{
  if (!(P1OUT&AT89_SS))
  {
    UART_Text("\r\nSPI START ERROR.\r\n");
    while (UART_Receive()!=3);
  }
  P1OUT&=~AT89_SS;
}

void SPI_Send(unsigned char data)
{
  static int i=0;
  i++;
  if (SPI_SendStop)
  {
    UART_Text("\r\nBUFFER NOT EMPTY.\r\n");
    while (UART_Receive()!=3);
  }
  if (P1OUT&AT89_SS)
  {
    UART_Text("\r\nSS NOT LOW.\r\n");
    UART_Hex(i);
    while (UART_Receive()!=3);
  }

  UC0IE&=~(UCB0TXIE|UCB0RXIE);
  if (SPI_RingReady)
  {
    UCB0TXBUF=data;
    SPI_RingReady=false;
    SPI_ReceiveCount++;
  }
  else
  {
    SPI_RingBuff[SPI_RingPtr]=data;
    SPI_RingPtr++;
    if (SPI_RingPtr==SPIBUFFERSIZE) SPI_RingPtr=0;
    SPI_RingCount++;
    SPI_ReceiveCount++;
    UC0IE|=UCB0TXIE|UCB0RXIE;
    while(SPI_RingCount==SPIBUFFERSIZE) UART_Send('!');//does this actually work?
  }
  UC0IE|=UCB0TXIE|UCB0RXIE;
}

unsigned char SPI_Receive()
{
  //UC0IE&=~UCB0RXIE;
  //SPI_ReceiveReady=2;
  SPI_Send('Z');
  //UC0IE|=UCB0RXIE;
  while (SPI_ReceiveCount)
  {
    //UART_Hex(SPI_ReceiveReady);
  }
  return SPI_ReceiveBuff;
}

void ProgDelayStop()
{
  UC0IE&=~(UCB0TXIE|UCB0RXIE);
  if (SPI_ReceiveCount) SPI_SendStop=true;
  else P1OUT|=AT89_SS;
  UC0IE|=UCB0TXIE|UCB0RXIE;
}

void ProgStop()
{
  //while (SPI_ReceiveCount);
  while (UCB0STAT & UCBUSY);
  //while (P2IN&1);
  __delay_cycles(5);//needed?
  P1OUT|=AT89_SS;
}

unsigned char CmdEnable()
{
  int i;
  unsigned char buff;
  ProgStart();
  SPI_Send(0xAA);
  SPI_Send(0x55);
  SPI_Send(0xAC);
  SPI_Send(0x53);
  buff=SPI_Receive();
  //UART_Hex(buff);
  ProgStop();
  return buff;
}

void CmdErase()
{
  ProgStart();
  SPI_Send(0xAA);
  SPI_Send(0x55);
  SPI_Send(0x8A);
  ProgStop();
  CmdPollBusy();
  return;
}

void CmdPollBusy()
{
  unsigned char buff;
  ProgStart();
  SPI_Send(0xAA);
  SPI_Send(0x55);
  SPI_Send(0x60);
  SPI_Send('X');
  SPI_Send('Y');
  do
  {
    buff=SPI_Receive();
    //UART_Hex(buff);
    //UART_Send(' ');
  }while(!(buff&1));
  ProgStop();
}

void delay_ms(int ms)
{
  while (ms--) __delay_cycles(16000);
}

