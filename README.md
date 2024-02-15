# espGarageController
esp8266 Garage Door Controller

Turn your esp8266 / NodeMCU v3 into a controller for your garagedoor.

Functions
* Open and Close the door
* Get door status; open, closed, moving
* Acess to functions and status through a simple REST API

Routes
/moveDoor - Move door to open or closed. This route allows you to control the door's position.
/doorStatus - Retrieve current status of the door. Use this to check if the door is open, moving or closed.
/deviceInfo - Retrieve basic info from the device. Provides essential information about the device.