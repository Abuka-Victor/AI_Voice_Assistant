#include "driver/i2s_std.h" 



// --- defines & macros --------

#ifndef DEBUG                   // user can define favorite behaviour ('true' displays addition info)
#  define DEBUG true            // <- define your preference here  
#  define DebugPrint(x);        if(DEBUG){Serial.print(x);}   /* do not touch */
#  define DebugPrintln(x);      if(DEBUG){Serial.println(x);} /* do not touch */ 
#endif


// --- PIN assignments ---------

#define I2S_WS      25          // add-on: L/R pin INMP441 on Vcc is RIGHT channel, connected to GND is LEFT channel
#define I2S_SD      33          
#define I2S_SCK     32     


// --- define your settings ----

#define SAMPLE_RATE             16000  // typical values: 8000 .. 44100, use e.g 8K (and 8 bit mono) for smallest .wav files  
                                       // hint: best quality with 16000 or 24000 (above 24000: random dropouts and distortions)
                                       // recommendation in case the STT service produces lot of wrong words: try 16000 

#define BITS_PER_SAMPLE         16      // 16 bit and 8bit supported (24 or 32 bits not supported)
                                       // hint: 8bit is less critical for STT services than a low 8kHz sample rate
                                       // for fastest STT: combine 8kHz and 8 bit. 

#define GAIN_BOOSTER_I2S        10     // original I2S streams is VERY silent, so we added an optional GAIN booster for INMP441
                                       // multiplier, values: 1-64 (32 seems best value for INMP441)
                                       // 64: high background noise but working well for STT on quiet human conversations

#define FLASH_RECORD_SIZE (1 * SAMPLE_RATE * BITS_PER_SAMPLE / 8 * 20)

i2s_std_config_t  std_cfg = 
{ .clk_cfg  =   // instead of macro 'I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),'
  { .sample_rate_hz = SAMPLE_RATE,
    .clk_src = I2S_CLK_SRC_DEFAULT,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
  },
  .slot_cfg =   // instead of macro I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
  { // hint: always using _16BIT because I2S uses 16 bit slots anyhow (even in case I2S_DATA_BIT_WIDTH_8BIT used !)
    .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,   // not I2S_DATA_BIT_WIDTH_8BIT or (i2s_data_bit_width_t) BITS_PER_SAMPLE  
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, 
    .slot_mode = I2S_SLOT_MODE_MONO,              // or I2S_SLOT_MODE_STEREO
    .slot_mask = I2S_STD_SLOT_RIGHT,              // use 'I2S_STD_SLOT_LEFT' in case L/R pin is connected to GND !
    .ws_width =  I2S_DATA_BIT_WIDTH_16BIT,           
    .ws_pol = false, 
    .bit_shift = true,   // using [.bit_shift = true] similar PHILIPS or PCM format (NOT 'false' as in MSB macro) ! ..
    .msb_right = false,  // .. or [.msb_right = true] to avoid overdriven and distorted signals on I2S microphones
  },
  .gpio_cfg =   
  { .mclk = I2S_GPIO_UNUSED,
    .bclk = (gpio_num_t) I2S_SCK,
    .ws   = (gpio_num_t) I2S_WS,
    .dout = I2S_GPIO_UNUSED,
    .din  = (gpio_num_t) I2S_SD,
    .invert_flags = 
    { .mclk_inv = false,
      .bclk_inv = false,
      .ws_inv = false,
    },
  },
};

// [re_handle]: global handle to the RX channel with channel configuration [std_cfg]
i2s_chan_handle_t  rx_handle;



struct WAV_HEADER 
{ char  riff[4] = {'R','I','F','F'};                        /* "RIFF"                                   */
  long  flength = 0;                                        /* file length in bytes                     ==> Calc at end ! */
  char  wave[4] = {'W','A','V','E'};                        /* "WAVE"                                   */
  char  fmt[4]  = {'f','m','t',' '};                        /* "fmt "                                   */
  long  chunk_size = 16;                                    /* size of FMT chunk in bytes (usually 16)  */
  short format_tag = 1;                                     /* 1=PCM, 257=Mu-Law, 258=A-Law, 259=ADPCM  */
  short num_chans = 1;                                      /* 1=mono, 2=stereo                         */
  long  srate = SAMPLE_RATE;                                /* samples per second, e.g. 44100           */
  long  bytes_per_sec = SAMPLE_RATE * (BITS_PER_SAMPLE/8);  /* srate * bytes_per_samp, e.g. 88200       */ 
  short bytes_per_samp = (BITS_PER_SAMPLE/8);               /* 2=16-bit mono, 4=16-bit stereo (byte 34) */
  short bits_per_samp = BITS_PER_SAMPLE;                    /* Number of bits per sample, e.g. 16       */
  char  dat[4] = {'d','a','t','a'};                         /* "data"                                   */
  long  dlength = 0;                                        /* data length in bytes (filelength - 44)   ==> Calc at end ! */
} myWAV_Header;

void wavHeader(byte* header, int wavSize){
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSize = wavSize + 44 - 8;
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;
  header[21] = 0x00;
  header[22] = 0x01;
  header[23] = 0x00;
  header[24] = 0x80;
  header[25] = 0x3E;
  header[26] = 0x00;
  header[27] = 0x00;
  header[28] = 0x00;
  header[29] = 0x7D;
  header[30] = 0x00;
  header[31] = 0x00;
  header[32] = 0x02;
  header[33] = 0x00;
  header[34] = 0x10;
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(wavSize & 0xFF);
  header[41] = (byte)((wavSize >> 8) & 0xFF);
  header[42] = (byte)((wavSize >> 16) & 0xFF);
  header[43] = (byte)((wavSize >> 24) & 0xFF); 
}

bool flg_is_recording = false;         // only internally used

bool flg_I2S_initialized = false;      // to avoid any runtime errors in case user forgot to initialize



// ------------------------------------------------------------------------------------------------------------------------------

bool I2S_Record_Init() 
{  
  // Get the default channel configuration by helper macro (defined in 'i2s_common.h')
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  
  i2s_new_channel(&chan_cfg, NULL, &rx_handle);     // Allocate a new RX channel and get the handle of this channel
  i2s_channel_init_std_mode(rx_handle, &std_cfg);   // Initialize the channel
  i2s_channel_enable(rx_handle);                    // Before reading data, start the RX channel first
  
  flg_I2S_initialized = true;                       // all is initialized, checked in procedure Record_Start()

  return flg_I2S_initialized;  
}



// ------------------------------------------------------------------------------------------------------------------------------
bool Record_Start( String audio_filename ) 
{
  if (!flg_I2S_initialized)     // to avoid any runtime error in case user missed to initialize
  {  Serial.println( "ERROR in Record_Start() - I2S not initialized, call 'I2S_Record_Init()' missed" );    
     return false;
  }
  
  if (!flg_is_recording)  // entering 1st time -> remove old AUDIO file, create new file with WAV header
  { 
    flg_is_recording = true;
    
    if (SPIFFS.exists(audio_filename)) 
    {  
      SPIFFS.remove(audio_filename); DebugPrintln("\n> Existing AUDIO file removed.");
    }  else {DebugPrintln("\n> No AUDIO file found");}
    
    // Kalo WAV header
    File audio_file = SPIFFS.open(audio_filename, FILE_WRITE);
    byte header[44];
    wavHeader(header, FLASH_RECORD_SIZE);
    audio_file.write(header, 44);
    // audio_file.write((uint8_t *) &myWAV_Header, 44);
    audio_file.close(); 
    
    DebugPrintln("> WAV Header generated, Audio Recording started ... ");
    return true;
  }
  
  if (flg_is_recording)  // here we land when recording started already -> task: append record buffer to file
  { 
    // Array to store Original audio I2S input stream (reading in chunks, e.g. 1024 values) 
    int16_t audio_buffer[1500];         // 1024 values [2048 bytes] <- for the original I2S signed 16bit stream 
    uint8_t audio_buffer_8bit[1500];    // 1024 values [1048 bytes] <- self calculated values in case BITS_PER_SAMPLE == 8

    // now reading the I2S input stream (with NEW <I2S_std.h>)
    size_t bytes_read = 0;
    i2s_channel_read(rx_handle, audio_buffer, sizeof(audio_buffer), &bytes_read, portMAX_DELAY);

    // Optionally: Boostering the very low I2S Microphone INMP44 amplitude (multiplying values with factor GAIN_BOOSTER_I2S)  
    if ( GAIN_BOOSTER_I2S > 1 && GAIN_BOOSTER_I2S <= 64 );    // check your own best values, recommended range: 1-64
    for (int16_t i = 0; i < ( bytes_read / 2 ); ++i)          // all 1024 values, 16bit (bytes_read/2) 
    {   audio_buffer[i] = audio_buffer[i] * GAIN_BOOSTER_I2S;  
    }

    if (BITS_PER_SAMPLE == 8) // in case we store a 8bit WAV file we fill the 2nd array with converted values
    { for (int16_t i = 0; i < ( bytes_read / 2 ); ++i)        
      { audio_buffer_8bit[i] = (uint8_t) ((( audio_buffer[i] + 32768 ) >>8 ) & 0xFF); 
      }
    }  
    
    // Save audio data to SD card (appending chunk array to file end)
    File audio_file = SPIFFS.open(audio_filename, FILE_APPEND);
    if (audio_file)
    {  
       if (BITS_PER_SAMPLE == 16) // 16 bit default: appending original I2S chunks (e.g. 1014 values, 2048 bytes)
       {  audio_file.write((uint8_t*)audio_buffer, bytes_read);
       }        
       if (BITS_PER_SAMPLE == 8)  // 8bit mode: appending calculated 1014 values instead (1024 bytes, 2048/2) 
       {  audio_file.write((uint8_t*)audio_buffer_8bit, bytes_read/2);
       }  
       audio_file.close(); 
       return true; 
    }  
    
    if (!audio_file) 
    { Serial.println("ERROR in Record_Start() - Failed to open audio file!"); 
      return false;
    }    
  }  
}



// ------------------------------------------------------------------------------------------------------------------------------
bool Record_Available( String audio_filename, float* audiolength_sec ) 
{
  if (!flg_is_recording) 
  {   return false;   
  }
  
  if (!flg_I2S_initialized)   // to avoid runtime errors: do nothing in case user missed to initialize at all
  {  return false;
  }
  
  // here we land when Recording is active .. task: stop recording, finalize WAV file, return true/done to main loop()
  if (flg_is_recording) 
  { 
    
    File audio_file = SPIFFS.open(audio_filename, "r+");
    long filesize = audio_file.size();
    audio_file.seek(0); myWAV_Header.flength = filesize;  myWAV_Header.dlength = (filesize-8);
    byte header[44];
    wavHeader(header, FLASH_RECORD_SIZE);
    // audio_file.write(header, 44);
    // audio_file.write((uint8_t *) &myWAV_Header, 44);
    audio_file.close(); 
    
    flg_is_recording = false;  // important: this is done only here
    
    *audiolength_sec = (float) (filesize-44) / (SAMPLE_RATE * BITS_PER_SAMPLE/8);   // return Audio length (via reference) 
     
    DebugPrintln("> ... Done. Audio Recording finished.");
    DebugPrint("> New AUDIO file: '" + audio_filename + "', filesize [bytes]: " + (String) filesize);
    DebugPrintln(", length [sec]: " + (String) *audiolength_sec);
    
    return true;   
  }  
}
