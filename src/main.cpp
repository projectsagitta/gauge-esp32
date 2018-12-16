/*
   Sagitta gauge ESP32 - cheap (C)TD sonde with WiFi WebServer.
   
   Thanks Hristo Gochkov for WebServer example.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <mySD.h>

#include <SPI.h>
#include <Adafruit_MAX31865.h>
#include <pt100rtd.h>
#include <ADS1115.h>
#define DBG_OUTPUT_PORT if (debug) Serial
#define DEFAULTFILENAME "datafile.sg"
struct sensorvalues {
        float temperature;
        float pressure;
        long station;
        long millis;
};

bool debug = true;
bool measuremode = false;
long iterate = 0;
sensorvalues zerovalues {0.0, 0.0, 0, 0};
String filename = DEFAULTFILENAME;
long unsigned int zeromillis = 0;

ADS1115 adc0(ADS1115_DEFAULT_ADDRESS);
Adafruit_MAX31865 maxRTD = Adafruit_MAX31865(15, 13, 12, 14);// Use software SPI: CS, DI, DO, CLK
#define RREF 430.0// The value of the Rref resistor. Use 430.0!
#define C2F(c) ((9 * c / 5) + 32)// Like, duh.
pt100rtd PT100 = pt100rtd();// init the Pt100 table lookup module

const char* ssid = "SagittaGauge";
const char* password = "Sagittarius";
const char* host = "sagitta";

WebServer server(80);

static bool hasSD = false;
File uploadFile;

void initPerytherials(){
        Wire.begin();
        DBG_OUTPUT_PORT.println("MAX31865 and ADS1115 multimeasuring test");
        DBG_OUTPUT_PORT.println("Initializing MAX31865...");
        maxRTD.begin(MAX31865_4WIRE); // set to 2WIRE or 4WIRE as necessary
        DBG_OUTPUT_PORT.println("Initializing ADS1115...");
        adc0.initialize(); // initialize ADS1115 16 bit A/D chip
        DBG_OUTPUT_PORT.println("Testing ADS1115 connections...");
        DBG_OUTPUT_PORT.println(adc0.testConnection() ? "ADS1115 connection successful" : "ADS1115 connection failed");
        adc0.showConfigRegister();
        adc0.setRate(ADS1115_RATE_860);
        adc0.setMode(ADS1115_MODE_CONTINUOUS);
}
void initWiFi(){
        //WIFI INIT
        DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
        WiFi.softAP(ssid, password);

        DBG_OUTPUT_PORT.println("Wi-Fi access point ready");
        DBG_OUTPUT_PORT.println();
        DBG_OUTPUT_PORT.print("IP address: ");
        DBG_OUTPUT_PORT.println(WiFi.softAPIP());
        DBG_OUTPUT_PORT.println();
        if (MDNS.begin(host)) {
                MDNS.addService("http", "tcp", 80);
                DBG_OUTPUT_PORT.println("MDNS responder started");
                DBG_OUTPUT_PORT.print("You can now connect to http://");
                DBG_OUTPUT_PORT.print(host);
                DBG_OUTPUT_PORT.println(".local");
        }
}

void returnOK() {
        server.send(200, "text/plain", "");
}
void returnFail(String msg) {
        server.send(500, "text/plain", msg + "\r\n");
}

String formatBytes(size_t bytes) {
        if (bytes < 1024) {
                return String(bytes) + "B";
        } else if (bytes < (1024 * 1024)) {
                return String(bytes / 1024.0) + "KB";
        } else if (bytes < (1024 * 1024 * 1024)) {
                return String(bytes / 1024.0 / 1024.0) + "MB";
        } else {
                return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
        }
}
bool loadFromSdCard(String path) {
        String dataType = "text/plain";
        if (path.endsWith("/")) {
                path += "index.htm";
        }

        if (path.endsWith(".src")) {
                path = path.substring(0, path.lastIndexOf("."));
        } else if (path.endsWith(".htm")) {
                dataType = "text/html";
        } else if (path.endsWith(".css")) {
                dataType = "text/css";
        } else if (path.endsWith(".js")) {
                dataType = "application/javascript";
        } else if (path.endsWith(".json")) {
                dataType = "application/json";
        } else if (path.endsWith(".sg")) {
                dataType = "application/json";
        } else if (path.endsWith(".png")) {
                dataType = "image/png";
        } else if (path.endsWith(".gif")) {
                dataType = "image/gif";
        } else if (path.endsWith(".jpg")) {
                dataType = "image/jpeg";
        } else if (path.endsWith(".ico")) {
                dataType = "image/x-icon";
        } else if (path.endsWith(".xml")) {
                dataType = "text/xml";
        } else if (path.endsWith(".pdf")) {
                dataType = "application/pdf";
        } else if (path.endsWith(".zip")) {
                dataType = "application/zip";
        }

        File dataFile = SD.open(path.c_str());
        if (dataFile.isDirectory()) {
                path += "/index.htm";
                dataType = "text/html";
                dataFile = SD.open(path.c_str());
        }

        if (!dataFile) {
                return false;
        }

        if (server.hasArg("download")) {
                dataType = "application/octet-stream";
        }

        if (server.streamFile(dataFile, dataType) != dataFile.size()) {
                DBG_OUTPUT_PORT.println("Sent less data than expected!");
        }

        dataFile.close();
        return true;
}
bool exists(String path) {
        bool yes = false;
        File file = SD.open(path.c_str(), 'r');
        if (!file.isDirectory()) {
                yes = true;
        }
        file.close();
        return yes;
}
void printDirectory() {
        if (!server.hasArg("dir")) {
                return returnFail("BAD ARGS");
        }
        String path = server.arg("dir");
        if (path != "/" && !SD.exists((char *)path.c_str())) {
                return returnFail("BAD PATH");
        }
        File dir = SD.open((char *)path.c_str());
        path = String();
        if (!dir.isDirectory()) {
                dir.close();
                return returnFail("NOT DIR");
        }
        dir.rewindDirectory();
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/json", "");
        WiFiClient client = server.client();

        server.sendContent("[");
        for (int cnt = 0; true; ++cnt) {
                File entry = dir.openNextFile();
                if (!entry) {
                        break;
                }

                String output;
                if (cnt > 0) {
                        output = ',';
                }
                output += "{\"type\":\"";
                output += (entry.isDirectory()) ? "dir" : "file";
                output += "\",\"name\":\"";
                output += entry.name();
                output += "\"";
                output += "}";
                server.sendContent(output);
                entry.close();
        }
        server.sendContent("]");
        dir.close();
}
int checkFault(void){
        // Check and print any faults
        uint8_t fault = maxRTD.readFault();
        if (fault)
        {
                DBG_OUTPUT_PORT.print("Fault 0x"); DBG_OUTPUT_PORT.println(fault, HEX);
                if (fault & MAX31865_FAULT_HIGHTHRESH) {DBG_OUTPUT_PORT.println("RTD High Threshold");}
                if (fault & MAX31865_FAULT_LOWTHRESH) {DBG_OUTPUT_PORT.println("RTD Low Threshold");}
                if (fault & MAX31865_FAULT_REFINLOW) {DBG_OUTPUT_PORT.println("REFIN- > 0.85 x Bias");}
                if (fault & MAX31865_FAULT_REFINHIGH) {DBG_OUTPUT_PORT.println("REFIN- < 0.85 x Bias - FORCE- open");}
                if (fault & MAX31865_FAULT_RTDINLOW) {DBG_OUTPUT_PORT.println("RTDIN- < 0.85 x Bias - FORCE- open");}
                if (fault & MAX31865_FAULT_OVUV) {DBG_OUTPUT_PORT.println("Under/Over voltage");}
                maxRTD.clearFault();
        }
        return fault;
}
String getJsonFromSens(sensorvalues sensorsdata){
        String json = "{";
        json += "\"station\":" + String(sensorsdata.station);
        json += ", \"millis\":" + String(sensorsdata.millis);
        //json += "\"heap\":" + String(ESP.getFreeHeap());
        json += ", \"temperature\":" + String(sensorsdata.temperature);
        json += ", \"pressure\":" + String(sensorsdata.pressure);
        json += "}";
        return json;
}
int getSensorsData(sensorvalues *sensorsdata){
        uint16_t rtd, ohmsx100;
        uint32_t dummy;
        float temperature;
        float pressure;
        rtd = maxRTD.readRTD();
        dummy = ((uint32_t)(rtd << 1)) * 100 * ((uint32_t) floor(RREF));
        dummy >>= 16;
        ohmsx100 = (uint16_t) (dummy & 0xFFFF);
        // or use exact ohms floating point value.
        //ohms = (float)(ohmsx100 / 100) + ((float)(ohmsx100 % 100) / 100.0);
        //DBG_OUTPUT_PORT.print("ohms: "); DBG_OUTPUT_PORT.print(ohms,2);
        temperature = PT100.celsius(ohmsx100); // NoobNote: LUT== LookUp Table
        //DBG_OUTPUT_PORT.print("\t| t = "); DBG_OUTPUT_PORT.print(temperature,3); DBG_OUTPUT_PORT.println(" C");
        checkFault();
        // Get the number of counts of the accumulator
        //DBG_OUTPUT_PORT.print("cnts: ");
        // The below method sets the mux and gets a reading.
        adc0.setGain(ADS1115_PGA_0P256);
        int sensorOneCounts=adc0.getConversionP0N1(); // counts up to 16-bits
        pressure = (float)sensorOneCounts/32.768;
        //DBG_OUTPUT_PORT.print(sensorOneCounts);
        // To turn the counts into a voltage, we can use
        //DBG_OUTPUT_PORT.print("\t| p = ");
        //DBG_OUTPUT_PORT.print(pressure); DBG_OUTPUT_PORT.println(" kPa");
        //DBG_OUTPUT_PORT.print("\t| ms = ");
        //DBG_OUTPUT_PORT.print(millis());
        //DBG_OUTPUT_PORT.println("\n-------------------------------");
        sensorsdata->millis = millis();
        sensorsdata->temperature = temperature;
        sensorsdata->pressure = pressure;
        return (0);
}

void handleFileUpload() {
        if (server.uri() != "/edit") {
                return;
        }
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
                if (SD.exists((char *)upload.filename.c_str())) {
                        SD.remove((char *)upload.filename.c_str());
                }
                uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
                DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(upload.filename);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (uploadFile) {
                        uploadFile.write(upload.buf, upload.currentSize);
                }
                DBG_OUTPUT_PORT.print("Upload: WRITE, Bytes: "); DBG_OUTPUT_PORT.println(upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
                if (uploadFile) {
                        uploadFile.close();
                }
                DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
        }
}
void deleteRecursive(String path) {
        File file = SD.open((char *)path.c_str());
        if (!file.isDirectory()) {
                file.close();
                SD.remove((char *)path.c_str());
                return;
        }

        file.rewindDirectory();
        while (true) {
                File entry = file.openNextFile();
                if (!entry) {
                        break;
                }
                String entryPath = path + "/" + entry.name();
                if (entry.isDirectory()) {
                        entry.close();
                        deleteRecursive(entryPath);
                } else {
                        entry.close();
                        SD.remove((char *)entryPath.c_str());
                }
                yield();
        }

        SD.rmdir((char *)path.c_str());
        file.close();
}
void handleDelete() {
        if (server.args() == 0) {
                return returnFail("BAD ARGS");
        }
        String path = server.arg(0);
        if (path == "/" || !SD.exists((char *)path.c_str())) {
                returnFail("BAD PATH");
                return;
        }
        deleteRecursive(path);
        returnOK();
}
void handleCreate() {
        if (server.args() == 0) {
                return returnFail("BAD ARGS");
        }
        String path = server.arg(0);
        if (path == "/" || SD.exists((char *)path.c_str())) {
                returnFail("BAD PATH");
                return;
        }
        if (path.indexOf('.') > 0) {
                File file = SD.open((char *)path.c_str(), FILE_WRITE);
                if (file) {
                        file.write(uint8_t(0));
                        file.close();
                        if (!file) {
                                DBG_OUTPUT_PORT.println("Cannot write file!");
                        }
                }
        } else {
                SD.mkdir((char *)path.c_str());
        }
        returnOK();
}
void handleFileRead() {
        if (!server.hasArg("dir")) {
                return returnFail("BAD ARGS");
        }
        String path = server.arg("dir");
        if (loadFromSdCard(path)) {
                return returnOK();
        }else{
                return returnFail("something went wrong...");
        }
}
void handleNotFound() {
        if (hasSD && loadFromSdCard(server.uri())) {
                return;
        }
        String message = "";
        if (!hasSD) {
                message += "SDCARD Not Detected\n";
        }
        message += "URI: ";
        message += server.uri();
        message += "\nMethod: ";
        switch (server.method()) {
        case 0b00000001: message += "HTTP_GET";
                break;
        case 0b00000010: message += "HTTP_POST";
                break;
        case 0b00000100: message += "HTTP_DELETE";
                break;
        case 0b00001000: message += "HTTP_PUT";
                break;
        case 0b00010000: message += "HTTP_PATCH";
                break;
        case 0b00100000: message += "HTTP_HEAD";
                break;
        case 0b01000000: message += "HTTP_OPTIONS";
                break;
        case 0b01111111: message += "HTTP_ANY";
                break;
        default: message += "UNKNOWN";
                break;
        }
        message += "\nArguments: ";
        message += server.args();
        message += "\n";
        for (uint8_t i = 0; i < server.args(); i++) {
                message += "\n NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
        }
        server.send(404, "text/plain", message);
        DBG_OUTPUT_PORT.print(message);
}
void handleZero(){
        sensorvalues sens;
        //calibrate sensors!
        sens.pressure = 0;
        sens.temperature = 0;
        float zeropressure = 0;
        float zerotemp = 0;
        for (int i = 0; i < 20; i++) {
                getSensorsData(&sens);
                zeropressure  +=  sens.pressure;
                zerotemp  +=  sens.temperature;
        }
        zeropressure = zeropressure / 20;
        zerotemp = zerotemp / 20;
        zerovalues.pressure = zeropressure;
        zerovalues.millis = millis();
        zerovalues.station = 0;
        sens.pressure = zeropressure;
        sens.temperature = zerotemp;
        sens.station = 0;
        iterate = 0;
        String jsonstring = getJsonFromSens(sens)+"\n";
        DBG_OUTPUT_PORT.print(jsonstring.c_str());
        server.send(300, "text/json", jsonstring);
}
void handleMeasureModeOn(){
        String coordinates = server.arg("coordinates");
        String datetime = server.arg("datetime");
        if (server.hasArg("zero")) {
                handleZero();
        }
        if (server.hasArg("dir")) {
                filename = server.arg("dir");
        } else {
                filename = DEFAULTFILENAME;
        }

        DBG_OUTPUT_PORT.println("\nServer args: " + String(server.args()) + "x");
        deleteRecursive(filename);
        measuremode = true;
        File sensorsdatafile;
        sensorsdatafile = SD.open(filename.c_str(), FILE_WRITE);
        //if (!server.hasArg("coordinates")||!server.hasArg("datetime")) {
        //        return returnFail("BAD ARGS");
        //}
        if (sensorsdatafile) {
                String json = "{\n";
                json += "\"coordinates\":\"" + coordinates + "\",\n";
                json += "\"datetime\":\"" + datetime + "\",\n";
                json += "\"data\":\n[";
                sensorsdatafile.print(json);
                DBG_OUTPUT_PORT.println(json);
                sensorsdatafile.flush();
                sensorsdatafile.close();
        }
        DBG_OUTPUT_PORT.print("\n\nnew measurement: ");
        DBG_OUTPUT_PORT.println(filename);
}
void handleMeasureModeOff(){
        measuremode = false;
        File sensorsdatafile;
        sensorsdatafile = SD.open(filename.c_str(), FILE_WRITE);
        delay(10);
        if (sensorsdatafile) {
                sensorsdatafile.print("\n]\n}\n");
                sensorsdatafile.flush();
                sensorsdatafile.close();
        }
        DBG_OUTPUT_PORT.print("\n\nend of measurement: ");
        DBG_OUTPUT_PORT.println(filename);
}
void measuring(){
        File sensorsdatafile;
        sensorsdatafile = SD.open(filename.c_str(), FILE_WRITE);//"test.txt", FILE_WRITE);//
        if (sensorsdatafile) {
                sensorvalues sensorval;
                sensorval.station = iterate;
                getSensorsData(&sensorval);
                String jsonstring = "";
                if (sensorval.station != 0) {
                        jsonstring += ",";
                } else {
                        zeromillis = millis();
                }
                sensorval.millis = sensorval.millis - zeromillis;
                sensorval.pressure = sensorval.pressure - zerovalues.pressure;
                jsonstring += "\n";
                jsonstring += getJsonFromSens(sensorval);
                sensorsdatafile.print(jsonstring);//.c_str());//, jsonstring.length());
                DBG_OUTPUT_PORT.print(jsonstring);//.c_str());
                sensorsdatafile.flush();
                sensorsdatafile.close();
                iterate++;
        }
        else {
                DBG_OUTPUT_PORT.println("cannot write file!");
                DBG_OUTPUT_PORT.println (filename);
        }
}

void setup(void) {
        DBG_OUTPUT_PORT.begin(115200);
        DBG_OUTPUT_PORT.print("\n");
        DBG_OUTPUT_PORT.setDebugOutput(true);

        initPerytherials();
        initWiFi();

        server.on("/list", HTTP_GET, printDirectory);
        server.on("/edit", HTTP_DELETE, handleDelete);
        server.on("/edit", HTTP_PUT, handleCreate);
        server.on("/edit", HTTP_POST, []() {
                returnOK();
        }, handleFileUpload);
        server.onNotFound(handleNotFound);
        server.on("/get", HTTP_GET, handleFileRead);
        server.on("/measuremode_on", HTTP_POST, []() {
                handleMeasureModeOn();
                returnOK();
        });
        server.on("/measuremode_off", HTTP_POST, []() {
                handleMeasureModeOff();
                returnOK();
        });
        server.on("/zero", HTTP_GET, []() {
                handleZero();
                returnOK();
        });
        server.on("/debug_on", HTTP_POST, []() {
                debug = true;
                DBG_OUTPUT_PORT.println("************ DEBUG SWITCHED ON ************");
                returnOK();
        });
        server.on("/debug_off", HTTP_POST, []() {
                DBG_OUTPUT_PORT.println("************ DEBUG SWITCHED OFF ************");
                debug = false;
                returnOK();
        });

        server.begin();
        DBG_OUTPUT_PORT.println("HTTP server started");

        if (SD.begin(13,15,2,14)) {
                DBG_OUTPUT_PORT.println("SD Card initialized.");
                hasSD = true;
        }
}
void loop(void) {
        server.handleClient();
        delay(100);
        if (measuremode) {
                measuring();
        }
}
