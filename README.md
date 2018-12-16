# gauge-esp32
Cheap (C)TD gauge with wifi and web-server based on market ESP32 and has pressure sensor, pt100 RTD ang hypotetical conductivity sensor.

For example:
    start measuring:
        ```
        POST request with args: d
        ```bash
        curl -d "dir=datafile.sg&coordinates=55.751244,37.618423&datetime=Sun Dec 16 11:39:40 2018 GMT&zero=true" "sagitta.local/measuremode_on"
        ```
