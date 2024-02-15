# espGarageController

## Overview

The espGarageController transforms your esp8266 / NodeMCU v3 into a smart controller for your garage door, offering features like opening and closing the door remotely and monitoring its status.

## Features

- **Control Your Garage Door**: Open and close your garage door with ease.
- **Door Status**: Check if the door is open, closed, or in motion.
- **REST API Access**: Utilize a simple REST API to control your garage door and get status updates.

## REST API Routes

### `/moveDoor`

- **Description**: Moves the garage door to the open or closed position.
- **Method**: `POST`
- **Parameters**: `action=open|close`

### `/doorStatus`

- **Description**: Retrieves the current status of the garage door.
- **Method**: `GET`
- **Response**: `{"status": "open|moving|closed"}`

### `/deviceInfo`

- **Description**: Provides essential information about the device.
- **Method**: `GET`
- **Response**: Basic device information.

## Getting Started

(Here you can add instructions on how to set up the espGarageController, including prerequisites, installation steps, and initial configuration.)

## Contributing

(If you're open to contributions, provide guidelines on how others can contribute to the project. This might include instructions for forking the repository, making changes, and submitting pull requests.)

## License

(Include information about the project's license here. If you're using an open-source license, like MIT, you can simply state that and include a copy of the license text in your repository.)

