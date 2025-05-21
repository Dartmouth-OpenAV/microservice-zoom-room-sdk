Copyright (C) 2024 Trustees of Dartmouth College

This project is licensed under the terms of the GNU General Public License (GPL), version 3 or later.

For alternative licensing options, please contact the Dartmouth College OpenAV project team.

# microservice-zoom-room-sdk

This microservice is for interacting with Zoom Rooms. It uses the [Zoom Room SDK](https://marketplacefront.zoom.us/sdk/zrc/index.html) to do so and is meant to replace the previous Zoom Room CLI microservice as Zoom's CLI will be deprecated in the near future.

## Interactive mode

This microservice is unique in that along with the usual REST API, it can be instantiated in interactive mode to be used for miscellaneous automation needs. To run in interactive mode, add the INTERACTIVE=true environment variable when running with Docker:

```docker run -ti -e INTERACTIVE=true  ghcr.io/dartmouth-openav/microservice-zoom-room-sdk:latest```

The first command you'll need to execute before any other is to pair with Zoom, you need to get your Activation Code from the Zoom Room's basic settings to do so.

![Zoom Room Activation Code](/activation_code.png)

```pair <activation_code>```

You can get a list of available commands using the `help` command. Everything should be pretty self-explanatory beyond that.

## Completeness

Only a minimal set of functionality from the SDK is implemented at the moment. We plan to expand based on need.
