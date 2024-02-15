# espGarageController

## Overview

The espGarageController transforms your esp8266 / NodeMCU v3 into a smart controller for your garage door, offering features like opening and closing the door remotely and monitoring its status.

## Features

- **Control Your Garage Door**: Open and close your garage door with ease.
- **Door Status**: Check if the door is open, closed, or in motion.
- **REST API Access**: Utilize a simple REST API to control your garage door and get status updates.

## Hardware
- esp8266 Nodemcu v3
- 1 x 3.3V Relay
- 2 x Magnetic Reed Sensor

## Homey
I use a own developed app for Homey Pro 2023 that is using this API to control the things.

## REST API Routes

### `/moveDoor`

- **Description**: Moves the garage door to the open or closed position.
- **Method**: `GET`

### `/doorStatus`

- **Description**: Retrieves the current status of the garage door.
- **Method**: `GET`

### `/deviceInfo`

- **Description**: Provides essential information about the device.
- **Method**: `GET`
- **Response**: Basic device information.
