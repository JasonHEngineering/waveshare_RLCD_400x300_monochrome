This V2 allows for passwords and apiKeys to be read from SD card. Therefore wifi SSID, passwords, bus stop IDs and apiKeys needs to be written into config.txt itself.
This does means that whenever an item above was changed, one has to only edit the config.txt rather than reprogramming the device.

On the other hand, if one desire to have passwords and apiKeys embedded directly into FW, refer to V1 instead.

Note the following tools settings in Arduino IDE (verified in 2.3.7).


<img width="600" height="1050" alt="image" src="https://github.com/user-attachments/assets/fdee8493-62f7-4b51-9330-78610d117484" />
