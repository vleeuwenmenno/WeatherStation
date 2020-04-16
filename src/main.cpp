#include <SPI.h>
#include <Thread.h>
#include <SD.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ############## Defines ##############

// How big our line buffer should be
#define BUFSIZ 96

#define TEMP_WIRE 7

// store error strings in flash to save RAM
#define error(s) error_P(PSTR(s))
 
#define SDCARD_CS 4 
#define WIZ_CS 10

//  ^^^^^^^^^^^^^ Defines ^^^^^^^^^^^^^

// ############## Vars ##############

Thread sensorReader = Thread();

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] = { 10, 0, 1, 202 };

EthernetServer server(80);

File root;

EthernetUDP ntpUDP;
NTPClient timeClient(ntpUDP);

OneWire oneWire(TEMP_WIRE);
DallasTemperature sensors(&oneWire);

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

String printDigits(int digits)
{
    String retval = "";
    // utility for digital clock display: prints preceding colon and leading 0
    if(digits < 10)
        retval += "0";

    retval += digits;
    return retval;
}

void digitalClockDisplay()
{
    // digital clock display of the time
    Serial.print(printDigits(hour()));
    Serial.print(":");
    Serial.print(printDigits(minute()));
    Serial.print(":");     
    Serial.print(printDigits(second()));

    Serial.print(" ");
    Serial.print(printDigits(day()));
    Serial.print("/");     
    Serial.print(printDigits(month()));
    Serial.print("/");
    Serial.print(year());
}

void sensorCallback()
{
    timeClient.update();
    time_t t = static_cast<time_t>(timeClient.getEpochTime());
    setTime(t);
    
    Serial.print("Log cycle ");
    digitalClockDisplay();

    String path = "/LOGS/" + String(year());    
    path += "/" + String(month());
    path += "/" + String(day());

    if (!SD.exists(path))
        SD.mkdir(path);

    File temps = SD.open(path + "/TEMP.LOG", FILE_WRITE);
    File press = SD.open(path + "/PRESSURE.LOG", FILE_WRITE);
    File wind = SD.open(path + "/WIND.LOG", FILE_WRITE);
    File rain = SD.open(path + "/RAIN.LOG", FILE_WRITE);

    sensors.requestTemperatures(); 
    
    float temp = sensors.getTempCByIndex(0);
    Serial.print("Celsius temperature: ");
    Serial.print(temp); 
    Serial.print(" ");

    temps.print(printDigits(hour()));
    temps.print(":");
    temps.print(printDigits(minute()));
    temps.print(":");     
    temps.print(printDigits(second()));
    temps.print("   ");
    temps.println(temp);

    press.print(printDigits(hour()));
    press.print(":");
    press.print(printDigits(minute()));
    press.print(":");     
    press.print(printDigits(second()));
    press.println("   TO_BE_IMPLEMENTED");

    wind.print(printDigits(hour()));
    wind.print(":");
    wind.print(printDigits(minute()));
    wind.print(":");     
    wind.print(printDigits(second()));
    wind.println("   TO_BE_IMPLEMENTED");

    rain.print(printDigits(hour()));
    rain.print(":");
    rain.print(printDigits(minute()));
    rain.print(":");     
    rain.print(printDigits(second()));
    rain.println("   TO_BE_IMPLEMENTED");

    temps.close();
    press.close();
    wind.close();
    rain.close();    

    Serial.print(" logs updated in ");
    Serial.println(path.c_str());
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

                    if (String(filename) == "" && SD.exists("/INDEX.HTM"))
                    {
                        // Any non-directory clicked, server will send file to client for download
                        client.println("Content-Type: text/html");
                        client.println();

                        filename = "INDEX.HTM";
                        file = SD.open(filename, O_READ);

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
                        break;
                    }

                    if (file.isDirectory()) 
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
                    else if (String(filename).endsWith(".LOG") || String(filename).endsWith(".TXT"))
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
	sensorReader.setInterval(10000);

    timeClient.begin();
}
 
void loop()
{
	if(sensorReader.shouldRun())
		sensorReader.run();

    webServerCallback();
}