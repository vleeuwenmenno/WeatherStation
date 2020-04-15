#include <SPI.h>
#include <Thread.h>
#include <SD.h>
#include <Ethernet.h>
#include <swRTC.h>

// ############## Defines ##############

// How big our line buffer should be
#define BUFSIZ 96

// store error strings in flash to save RAM
#define error(s) error_P(PSTR(s))
 
#define SDCARD_CS 4 
#define WIZ_CS 10

//  ^^^^^^^^^^^^^ Defines ^^^^^^^^^^^^^

// ############## Vars ##############

Thread sensorReader = Thread();

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] = { 10, 0, 1, 248 };

EthernetServer server(80);

File root;
swRTC rtc;

//  ^^^^^^^^^^^^^ Vars ^^^^^^^^^^^^^


void error_P(const char* str) 
{
  Serial.print(F("error: "));
  Serial.println(str);
 
  while(1);
}
 
void ListFiles(EthernetClient client, uint8_t flags, File dir) 
{
    client.println("<ul>");
    while (true) 
    {
        File entry = dir.openNextFile();

        // done if past last used entry
        if (! entry) 
        {
            // no more files
            break;
        }

        // print any indent spaces
        client.print("<li><a href=\"");
        client.print(entry.name());
        if (entry.isDirectory()) 
        {
            client.println("/");
        }
        client.print("\">");

        // print file name with possible blank fill
        client.print(entry.name());
        if (entry.isDirectory()) 
        {
            client.println("/");
        }
            
        client.print("</a>");

        client.println("</li>");
        entry.close();
    }
    client.println("</ul>");
}

void printDirectory(File dir, int numTabs) 
{
   while(true) {
     File entry =  dir.openNextFile();
     if (! entry) {
       // no more files
       break;
     }
     for (uint8_t i=0; i<numTabs; i++) {
       Serial.print('\t');
     }
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println("/");
       printDirectory(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       Serial.print("\t\t");
       Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
}

void printDigits(int digits)
{
    Serial.print(":");

    if(digits < 10)
        Serial.print('0');

    Serial.print(digits);
}

void printDateTime()
{
    Serial.print(rtc.getHours());
    printDigits(rtc.getMinutes());
    printDigits(rtc.getSeconds());
    Serial.print(" ");
    Serial.print(rtc.getDay());
    Serial.print(" ");
    Serial.print(rtc.getMonth());
    Serial.print(" ");
    Serial.print(rtc.getYear());
    Serial.println();
}

void sensorCallback()
{
	Serial.print(F("DateTime: "));
    printDateTime();
}

void webServerCallback()
{
    char clientline[BUFSIZ];
    char name[17];
    int index = 0;
    
    EthernetClient client = server.available();
    if (client) 
    {
        // reset the input buffer
        index = 0;
        
        while (client.connected()) 
        {
            if (client.available()) 
            {
                char c = client.read();
                
                // If it isn't a new line, add the character to the buffer
                if (c != '\n' && c != '\r') 
                {
                    clientline[index] = c;
                    index++;
                    // are we too big for the buffer? start tossing out data
                    if (index >= BUFSIZ) 
                        index = BUFSIZ -1;
                    
                    // continue to read more data!
                    continue;
                }
                
                // got a \n or \r new line, which means the string is done
                clientline[index] = 0;
                
                // Print it out for debugging
                Serial.println(clientline);
                
                // Look for substring such as a request to get the file
                if (strstr(clientline, "GET /") != 0) 
                {
                    // this time no space after the /, so a sub-file!
                    char *filename;
                    
                    filename = clientline + 5; // look after the "GET /" (5 chars)  *******
                    // a little trick, look for the " HTTP/1.1" string and 
                    // turn the first character of the substring into a 0 to clear it out.
                    (strstr(clientline, " HTTP"))[0] = 0;
            
                    if(filename[strlen(filename)-1] == '/') {  // Trim a directory filename
                        filename[strlen(filename)-1] = 0;        //  as Open throws error with trailing /
                    }
                    
                    Serial.print(F("Web request for: ")); Serial.println(filename);  // print the file we want
            
                    File file = SD.open(filename, O_READ);
                    if ( file == 0 ) 
                    {  
                        // Opening the file with return code of 0 is an error in SDFile.open
                        client.println("HTTP/1.1 404 Not Found");
                        client.println("Content-Type: text/html");
                        client.println();
                        client.println("<h2>File Not Found!</h2>");
                        client.println("<br><h3>Couldn't open the File!</h3>");
                        break; 
                    }
                    
                    Serial.println("File download begun...");
                                
                    client.println("HTTP/1.1 200 OK");

                    if (String(filename) == "")
                    {
                        // Any non-directory clicked, server will send file to client for download
                        client.println("Content-Type: text/html");
                        client.println();

                        filename = "INDEX.HTM";
                        file = SD.open(filename, O_READ);
                    }
                    else if (file.isDirectory()) 
                    {
                        Serial.println("is a directory");
                        client.println("Content-Type: text/html");
                        client.println();
                        client.print("<h2>Files in /");
                        client.print(filename); 
                        client.println(":</h2>");

                        ListFiles(client,LS_SIZE,file); 

                        file.close();              
                    } 
                    else if (String(filename).endsWith(".TXT"))
                    {
                        // Any non-directory clicked, server will send file to client for download
                        client.println("Content-Type: text/plain");
                        client.println();
                    }
                    else if (String(filename).endsWith(".HTM") || String(filename).endsWith(".HTML"))
                    {
                        // Any non-directory clicked, server will send file to client for download
                        client.println("Content-Type: text/html");
                        client.println();
                    }
                    else 
                    { 
                        // Any non-directory clicked, server will send file to client for download
                        client.println("Content-Type: application/octet-stream");
                        client.println();
                    }

                    char file_buffer[16];
                    int avail;
                    while (avail = file.available()) 
                    {
                        int to_read = min(avail, 16);
                        if (to_read != file.read(file_buffer, to_read)) 
                            break;
                        
                        client.write(file_buffer, to_read);
                    }
                    file.close();
                } 
                else 
                {
                    // everything else is a 404
                    client.println("HTTP/1.1 404 Not Found");
                    client.println("Content-Type: text/html");
                    client.println();
                    client.println("<h2>File Not Found!</h2>");
                }
                break;
            }
        }
        // give the web browser time to receive the data
        delay(1);
        client.stop();
    }
}

void setup()
{
    Serial.begin(9600);
    while (!Serial);      // For 32u4 based microcontrollers like 32u4 Adalogger Feather
    
    //Serial.print(F("Free RAM: ")); Serial.println(FreeRam());  
    
    if (!SD.begin(SDCARD_CS)) 
    {
        error("card.init failed!");
    } 
    
    root = SD.open("/");
    Serial.println(F("Done"));
    
    printDirectory(root, 0);

    // Recursive list of all directories
    Serial.println(F("Files found in all dirs:"));
    printDirectory(root, 0);

    Serial.println(F("Initializing web server..."));
    Ethernet.init(WIZ_CS);

    // Give the ethernet module time to boot up
    delay(1000);

    // Start the Ethernet connection
    Ethernet.begin(mac, ip);
    
    // Print the Ethernet board/shield's IP address to Serial monitor
    Serial.print(F("Serving on IP address: "));
    Serial.println(Ethernet.localIP());
    server.begin();

    // Begin sensor reading thread
    sensorReader.onRun(sensorCallback);
	sensorReader.setInterval(5000);

    rtc.setDate(16, 05, 2020);
    rtc.setTime(0, 30, 0);
    rtc.startRTC();

    Serial.print(F("Timestamp: "));
    Serial.println(rtc.getTimestamp());
}
 
void loop()
{
	if(sensorReader.shouldRun())
		sensorReader.run();

    webServerCallback();
}