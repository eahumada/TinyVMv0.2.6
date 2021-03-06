
#include <rom.h>

#include "types.h"
#include "constants.h"
#include "classes.h"
#include "threads.h"
#include "specialclasses.h"
#include "specialsignatures.h"
#include "language.h"
#include "memory.h"
#include "interpreter.h"
#include "exceptions.h"
#include "configure.h"
#include "trace.h"
#include "magic.h"
#include "systime.h"

extern char _bss_start;
extern char _end;
extern char _text_end;
extern char _data_end;
extern char _text_begin;
extern char _data_begin;
extern char _extraMemory;

/**
 * bootThread is a special global.
 */
Thread *bootThread;

#if !EMULATE && VERIFY
#error Should not VERIFY in RCX
#endif

#define MEM_START      (&_end)
#define RAM_START_ADDR 0x8000
#define RAM_SIZE       0x6F00

#define MEM_END_ADDR   (RAM_START_ADDR + RAM_SIZE)
#define MEM_END        ((char *) MEM_END_ADDR)
#define BUFSIZE        9
#define TDATASIZE      100
#define MAXNEXTBYTEPTR (MEM_END - TDATASIZE)

#define HC_NONE            0
#define HC_SHUTDOWN_POWER  1
#define HC_DELETE_PROGRAM  2

#define HP_NO_PROGRAM      0
#define HP_INTERRUPTED     1
#define HP_NOT_STARTED     2

#define POWER_OFF_TIMEOUT  (8L * 60L * 1000L)

#ifdef VERIFY

void assert (boolean aCond, int aCode)
{
  // TBD
}

#endif

char versionArray[] = { 0xE2, 0x00, 0x03, 0x00, 0x01, 
                        0x00, 0x00, 0x00, 0x00 };

char timerdata1[6];
async_t timerdata0;
char buffer[BUFSIZE];
char numread;   
char valid;
char hookCommand;
char *nextByte;
char *mmStart;
short status;
byte hasProgram;
boolean isReadyToTransfer;
TWOBYTES seqNumber;
TWOBYTES currentIndex;
unsigned long powerOffTime;

static inline void
set_data_pointer (void *ptr)
{
  play_sound_or_set_data_pointer(0x1771, (short)ptr, 0);
}

void wait_for_power_release()
{
  do
  {
    get_power_status (POWER_KEY, &status);
  } while (status == 0);
}

void trace (short s, short n1, short n2)
{
  if (s != -1)
    play_system_sound (SOUND_QUEUED, s);
  set_lcd_number (LCD_UNSIGNED, n1, 3002);
  set_lcd_number (LCD_PROGRAM, n2, 0);
  set_lcd_segment (LCD_SENSOR_1_VIEW);
  refresh_display();
  // Wait for power press
  do {
    get_power_status (POWER_KEY, &status);
  } while (status != 0);
  // Wait for power release
  wait_for_power_release();
}

/**
 * This function is invoked by switch_thread
 * and the download loop.
 */
void switch_thread_hook()
{
  get_power_status (POWER_KEY, &status);
  if (!status)
  {
    // Power button pressed - wait for release
    wait_for_power_release();
    read_buttons (0x3000, &status);
    if (status & 0x04)
    {
      hookCommand = HC_DELETE_PROGRAM;
      // Force interpreter to exit. The program must
      // be deleted from memory as a result.
      gMustExit = true;
    }
    else
    {
      hookCommand = HC_SHUTDOWN_POWER;
      gRequestSuicide = true;
      // The following sound should not be heard unless
      // the program did not commit suicide (dealock or
      // cathing of Throwable around infinite loop).
      play_system_sound (SOUND_QUEUED, 2);
    }
  }
}

void reset_rcx_serial()
{
  //init_serial (&timerdata1[4], &timerdata0, 1, 1);
  init_serial (0, 0, 1, 1);
}

int main (void)
{
 LABEL_FIRMWARE_ONE_TIME_INIT:
  hasProgram = HP_NO_PROGRAM;
 LABEL_POWERUP:
  // The following call always needs to be the first one.
  init_timer (&timerdata0, &timerdata1[0]);
  sys_time = 0l;
  init_power();
  systime_init();
  init_sensors();
  // If power key pressed, wait until it's released.
  wait_for_power_release();
  hookCommand = HC_NONE;
 LABEL_DOWNLOAD:
  reset_rcx_serial();
 LABEL_DOWNLOAD_INIT:
  isReadyToTransfer = false;
  play_system_sound (SOUND_QUEUED, 1);
  clear_display();
  get_power_status (POWER_BATTERY, &status);
  set_lcd_number (LCD_UNSIGNED, status, 3002);
  set_lcd_number (LCD_PROGRAM, (short) 0, 0);
 LABEL_START_TRANSFER:
  powerOffTime = sys_time + POWER_OFF_TIMEOUT;
  clear_lcd_segment (LCD_WALKING);
  if (hasProgram != HP_NO_PROGRAM)
  {
    set_lcd_segment (LCD_STANDING);
  }
  else
  {
    clear_lcd_segment (LCD_STANDING);
  }
  refresh_display();
 LABEL_RESET_TRANSFER:
  currentIndex = 1;
 LABEL_COMM_LOOP:
  for (;;)
  {
    check_for_data (&valid, &nextByte);
    if (valid)
    {
      //if (runStatus != RS_NO_PROGRAM)
      //  set_run_status (RS_NO_PROGRAM);
      numread = 0;
      receive_data (buffer, BUFSIZE, &numread);
      switch (buffer[0] & 0xF7)
      {
        case 0x10:
          // Alive?
          buffer[0] = ~buffer[0];
          send_data (SERIAL_NO_POINTER, 0, buffer, 1);
          goto LABEL_COMM_LOOP;
        case 0x15:
          // Get Versions...
          versionArray[0] = ~buffer[0];
          send_data (SERIAL_NO_POINTER, 0, versionArray, 9);
          goto LABEL_COMM_LOOP;
        case 0x65:
          // Delete firmware
          buffer[0] = ~buffer[0];
          send_data (SERIAL_NO_POINTER, 0,  buffer, 1);
          goto LABEL_EXIT;
        case 0x45:
          // Transfer data
          seqNumber = ((TWOBYTES) buffer[2] << 8) |  buffer[1]; 
	  buffer[0] = ~buffer[0];
	  buffer[1] = (byte) !isReadyToTransfer;
	  send_data (SERIAL_NO_POINTER, 0, buffer, 2);
	  if (!isReadyToTransfer)
	  {
  	    goto LABEL_DOWNLOAD;
	  }
          break;
	case 0x12:
          // Get value -- used by TinyVM to initiate download
	  if (isReadyToTransfer)
            goto LABEL_DOWNLOAD;
	  buffer[0] = ~buffer[0];
	  buffer[1] = (byte) (MAGIC >> 8);
	  buffer[2] = (byte) (MAGIC & 0xFF);
	  send_data (SERIAL_NO_POINTER, 0, buffer, 3);
          set_data_pointer (MEM_START);
          isReadyToTransfer = true;
	  hasProgram = HP_NO_PROGRAM;
	  goto LABEL_START_TRANSFER;
        default:
          // Other??
          #if 0
          trace (2, (short) buffer[0], 7);
          #endif
          goto LABEL_DOWNLOAD;
      }
      set_lcd_number (LCD_UNSIGNED, (short) currentIndex, 3002);
      set_lcd_number (LCD_PROGRAM, (short) 0, 0);
      refresh_display();
      if (currentIndex != 1 && seqNumber == 0)
      {
        // Reinitialize serial communications
        reset_rcx_serial();
	// Set pointer to start of binary image
        install_binary (MEM_START);
        // Check magic number
        if (get_magic_number() != MAGIC)
        { 
          trace (1, MAGIC, 9);
          goto LABEL_DOWNLOAD;
        }
	// Indicate that the RCX has a new program in it.
	hasProgram = HP_NOT_STARTED;
        // Make sure memory allocation starts at an even address.
        mmStart = (((TWOBYTES) nextByte) & 0x0001) ? nextByte + 1 : nextByte;
        // Initialize memory for object allocation.
	// This can only be done once for each program downloaded.
        init_memory (mmStart, ((TWOBYTES) MEM_END - (TWOBYTES) mmStart) / 2);
        // Initialize special exceptions
        init_exceptions();
        // Create the boot thread (bootThread is a special global).
        bootThread = (Thread *) new_object_for_class (JAVA_LANG_THREAD);
        goto LABEL_DOWNLOAD_INIT;
      }
      if (nextByte >= MAXNEXTBYTEPTR || seqNumber != currentIndex)
      {
        #if 0
        trace (4, (TWOBYTES) seqNumber, currentIndex);
        trace (4, (TWOBYTES) nextByte / 10, 5);
        #endif
        goto LABEL_DOWNLOAD;
      }
      currentIndex++;
    }
    else
    {
      switch_thread_hook();
      if (hookCommand == HC_SHUTDOWN_POWER)
        goto LABEL_SHUTDOWN_POWER;
      if (hookCommand == HC_DELETE_PROGRAM)
	goto LABEL_FIRMWARE_ONE_TIME_INIT;
      if (hasProgram != HP_NO_PROGRAM)
      {
        read_buttons (0x3000, &status);
	if ((status & 0x01) != 0)
	{
          do {
            read_buttons (0x3000, &status);
	  } while ((status & 0x01) != 0);
          goto LABEL_PROGRAM_STARTUP;
	}
      }
      if (sys_time >= powerOffTime)
      {
	play_system_sound (SOUND_QUEUED, 0);
	for (status = 0; status < 30000; status++) { }
	goto LABEL_SHUTDOWN_POWER;
      }
    }
  }

 LABEL_PROGRAM_STARTUP:

  // Jump to this point to start executing main().
  // Initialize the threading module.
  init_threads();
  // Start/prepare boot thread. This schedules execution of main().
  if (!init_thread (bootThread))
  {
    // There isn't enough memory to even start the boot thread!!
    trace (1, START_V, JAVA_LANG_OUTOFMEMORYERROR % 10);
    // The program is useless now
    goto LABEL_FIRMWARE_ONE_TIME_INIT;
  }
  // Show walking man.
  clear_lcd_segment (LCD_STANDING);
  set_lcd_segment (LCD_WALKING);
  refresh_display();  
  // Execute the bytecode interpreter.
  engine();
  // Engine returns when all threads are done or
  // when power has been shut down. The following
  // delay is so that motors get a chance to stop.
  for (status = 0; status < 10000; status++) { }
  if (hookCommand == HC_DELETE_PROGRAM)
  {
    // Program must be deleted from memory
    goto LABEL_FIRMWARE_ONE_TIME_INIT;
  }
  // Getting to this point means that the program
  // finished voluntarily, or as a result of an
  // assisted suicide.
  hasProgram = HP_NOT_STARTED;
  if (hookCommand != HC_SHUTDOWN_POWER)
    goto LABEL_DOWNLOAD;
 LABEL_SHUTDOWN_POWER:
  clear_display();
  refresh_display();
  shutdown_sensors();
  shutdown_buttons();
  shutdown_timer();
  shutdown_power();
  goto LABEL_POWERUP;
 LABEL_EXIT:
  // Seems to be a good idea to shutdown timer before going back to ROM.
  shutdown_timer();
  return 0;
}

