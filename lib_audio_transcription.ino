#include <WiFiClientSecure.h>  
#include "Credentials.h"
// #include <SPIFFS.h>

#ifndef DEBUG                   // user can define favorite behaviour ('true' displays addition info)
#  define DEBUG true            // <- define your preference here  
#  define DebugPrint(x);        if(DEBUG){Serial.print(x);}   /* do not touch */
#  define DebugPrintln(x);      if(DEBUG){Serial.println(x);} /* do not touch */ 
#endif

const char* deepgramApiKey =    DEEPGRAM_KEY;                     // ## INSERT your Deepgram credentials !

#define STT_LANGUAGE      "en-US"  // forcing single language: e.g. "de" (German), reason: improving recognition quality
                                // keep EMPTY ("") if you want Deepgram to detect & understand 'your' language automatically, 
                                // language abbreviation examples: "en", "en-US", "en-IN", "de" etc.
                                // all see here: https://developers.deepgram.com/docs/models-languages-overview

#define TIMEOUT_DEEPGRAM   6   // define your preferred max. waiting time [sec] for Deepgram transcription response     

#define STT_KEYWORDS            "&keywords=KALO&keywords=Janthip&keywords=Google"  // optional, forcing STT to listen exactly 
/*#define STT_KEYWORDS          "&keywords=KALO&keywords=Sachin&keywords=Google"   // optional, forcing STT to listen exactly */


// --- global vars -------------
WiFiClientSecure client;       



// ----------------------------------------------------------------------------------------------------------------------------

String SpeechToText_Deepgram( String audio_filename )
{ 
  uint32_t t_start = millis(); 
  
  // ---------- Connect to Deepgram Server (only if needed, e.g. on INIT and after lost connection)

  if ( !client.connected() )
  { DebugPrintln("> Initialize Deepgram Server connection ... ");
    client.setInsecure();
    /* no effect: client.setConnectionTimeout(4000); */   
    if (!client.connect("api.deepgram.com", 443)) 
    { Serial.println("\nERROR - WifiClientSecure connection to Deepgram Server failed!");
      client.stop(); /* might not have any effect, similar with client.clear() */
      return ("");  // in rare cases: WiFiClientSecure freezes (library issue?) 
    }
    DebugPrintln("Done. Connected to Deepgram Server.");
  }
  uint32_t t_connected = millis();  

        
  // ---------- Check if AUDIO file exists, check file size 
  
  File audioFile = SPIFFS.open( audio_filename );    
  if (!audioFile) {
    Serial.println("ERROR - Failed to open file for reading");
    return ("");
  }
  size_t audio_size = audioFile.size();
  audioFile.close();
  DebugPrintln("> Audio File [" + audio_filename + "] found, size: " + (String) audio_size );

  String socketcontent = "";
  while (client.available()) 
  { char c = client.read(); socketcontent += String(c);
  } int RX_flush_len = socketcontent.length(); 

  String optional_param;                          // see: https://developers.deepgram.com/docs/stt-streaming-feature-overview
  optional_param =  "?model=nova-2-general";      // Deepgram recommended model (high readability, lowest word error rates)
  optional_param += (STT_LANGUAGE != "") ? ("&language="+(String)STT_LANGUAGE) : ("&detect_language=true");  // see #defines  
  optional_param += "&smart_format=true";         // applies formatting (Punctuation, Paragraphs, upper/lower etc ..) 
  optional_param += "&numerals=true";             // converts numbers from written to numerical format (works with 'en' only)
  optional_param += STT_KEYWORDS;                 // optionally too: keyword boosting on multiple custom vocabulary words
  
  client.println("POST /v1/listen" + optional_param + " HTTP/1.1"); 
  client.println("Host: api.deepgram.com");
  client.println("Authorization: Token " + String(deepgramApiKey));
  client.println("Content-Type: audio/wav");
  client.println("Content-Length: " + String(audio_size));
  client.println();   // header complete, now sending binary body (wav bytes) .. 
  
  DebugPrintln("> POST Request to Deepgram Server started, sending WAV data now ..." );
  uint32_t t_headersent = millis();     
  
  File file = SPIFFS.open( audio_filename, FILE_READ );
  const size_t bufferSize = 2048;      // best values seem anywhere between 1024 and 2048; 
  uint8_t buffer[bufferSize];
  size_t bytesRead;

  while (file.available()) 
  { bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead > 0) {client.write(buffer, bytesRead);}   // sending WAV AUDIO data       
  }

  file.close();
  DebugPrintln("> All bytes sent, waiting Deepgram transcription");
  uint32_t t_wavbodysent = millis();  
 
  String response = "";   // waiting until available() true and all data completely received
  while ( response == "" && millis() < (t_wavbodysent + TIMEOUT_DEEPGRAM*1000) )   
  { while (client.available())                         
    { char c = client.read();
      response += String(c);      
    }
    // printing dots '.' each 100ms while waiting response 
    DebugPrint(".");  delay(100);           
  } 
  DebugPrintln();
  if (millis() >= (t_wavbodysent + TIMEOUT_DEEPGRAM*1000))
  { Serial.print("\n*** TIMEOUT ERROR - forced TIMEOUT after " + (String) TIMEOUT_DEEPGRAM + " seconds");
    Serial.println(" (is your Deepgram API Key valid ?) ***\n");    
  } 
  uint32_t t_response = millis();  


  // ---------- closing connection to Deepgram 
  client.stop();     // observation: unfortunately needed, otherwise the 'audio_play.openai_speech() in AUDIO.H not working !
  
  int    response_len  = response.length();
  String transcription = json_object( response, "\"transcript\":" );
  String language      = json_object( response, "\"detected_language\":" );
  String wavduration   = json_object( response, "\"duration\":" );

  DebugPrintln( "---------------------------------------------------" );
  DebugPrintln( "-> Latency Server (Re)CONNECT [t_connected]:   " + (String) ((float)((t_connected-t_start))/1000) );;   
  DebugPrintln( "-> Latency sending HEADER [t_headersent]:      " + (String) ((float)((t_headersent-t_connected))/1000) );   
  DebugPrintln( "-> Latency sending WAV file [t_wavbodysent]:   " + (String) ((float)((t_wavbodysent-t_headersent))/1000) );   
  DebugPrintln( "-> Latency DEEPGRAM response [t_response]:     " + (String) ((float)((t_response-t_wavbodysent))/1000) );   
  DebugPrintln( "=> TOTAL Duration [sec]: ..................... " + (String) ((float)((t_response-t_start))/1000) ); 
  DebugPrintln( "=> RX data prior request [bytes]: " + (String) RX_flush_len );
  DebugPrintln( "=> Server detected audio length [sec]: " + wavduration );
  DebugPrintln( "=> Server response length [bytes]: " + (String) response_len );
  DebugPrintln( "=> Detected language (optional): [" + language + "]" ); 
  DebugPrintln( "=> Transcription: [" + transcription + "]" );
  DebugPrintln( "---------------------------------------------------\n" );
  return transcription;    
}
int x=0;
void Deepgram_KeepAlive()        // should be called each 5 seconds about (to overcome the default autoclosing after 10 secs)
{

  led_RGB(0, 50, 0);
  uint32_t t_start = millis();  
  DebugPrint( "* Deepgram KeepAlive | " );
   
  // ---------- Connect to Deepgram Server (on INIT and every time in case closed)
  
  if ( !client.connected() )
  { DebugPrint("NEW Reconnection ... ");
    client.setInsecure();
    /* no effect: client.setConnectionTimeout(4000); */   
    if (!client.connect("api.deepgram.com", 443)) 
    { Serial.println("\n* PING Error - Server Connection failed.");
    x++;
    if (x > 2) {
    esp_restart(); // Reset the ESP32
    }
      return;  // in rare cases: WiFiClientSecure freezes (library issue?)  
    }  
    DebugPrint( "Done, connected.  -->  Connect Latency [sec]: ");  
    DebugPrintln( (String)((float)((millis()-t_start))/1000) );  
    led_RGB(0, 0, 0);
    return;   // done, on next cycle (after e.g. 5 secs) we ping data
  }
  
  uint8_t empty_wav[] = {
  0x52,0x49,0x46,0x46, 0x40,0x00,0x00,0x00, 0x57,0x41,0x56,0x45,0x66,0x6D,0x74,0x20, 
  0x10,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x80,0x3E,0x00,0x00,0x80,0x3E,0x00,0x00,
  0x01,0x00,0x08,0x00,0x64,0x61,0x74,0x61, 0x14,0x00,0x00,0x00, 0x80,0x80,0x80,0x80, 
  0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80 }; 
  
  client.println("POST /v1/listen HTTP/1.1"); 
  client.println("Host: api.deepgram.com");
  client.println("Authorization: Token " + String(deepgramApiKey));
  client.println("Content-Type: audio/wav");
  client.println("Content-Length: " + String(sizeof(empty_wav)));
  client.println(); // header complete, now sending wav bytes .. 
  client.write(empty_wav, sizeof(empty_wav)); 
  
  String response = "";
  while (client.available()) 
  { char c = client.read(); response += String(c);
  } int RX_len = response.length(); 

  DebugPrint( "TX (WAV): " + (String) sizeof(empty_wav) + " bytes | " );
  DebugPrint( "RX (TXT): " + (String) RX_len + " bytes  -->  " );
  DebugPrintln( "Total Latency [sec]: " + (String) ((float)((millis()-t_start))/1000) );     
}

String json_object( String input, String element )
{ String content = "";
  int pos_start = input.indexOf(element);      
  if (pos_start > 0)                                      // if element found:
  {  pos_start += element.length();                       // pos_start points now to begin of element content     
     int pos_end = input.indexOf( ",\"", pos_start);      // pos_end points to ," (start of next element)  
     if (pos_end > pos_start)                             // memo: "garden".substring(from3,to4) is 1 char "d" ..
     { content = input.substring(pos_start,pos_end);      // .. thats why we use for 'to' the pos_end (on ," ):
     } content.trim();                                    // remove optional spaces between the json objects
     if (content.startsWith("\""))                        // String objects typically start & end with quotation marks "    
     { content=content.substring(1,content.length()-1);   // remove both existing quotation marks (if exist)
     }     
  }  
  return (content);
}
