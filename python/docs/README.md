# Introduction

The Scorbit API is organized around a combination REST and socket connections. The API evolves in real-time, so check here for updates that could impact functionality.

The purpose of the API is to allow a remote machine to:

* Authenticate with a proven identity, where every machine is unique
* Send game data to Scorbit in real-time
* Send final game scores to Scorbit
* Allow applications to query leaderboard, game and player information
* Allow for the creation and management of game-specific achievements, notifications, or other information

The API is also used in communicating with the Scorbit app to allow the installation and pairing of game machines or virtual machines, interface with players, and allow other interactions with Scorbit data.

The connection of a new machine follows this basic process:

* Creation of a secure unique identifier for the game which is stored locally on the game.
* Association of the unique identifier with a virtual version of the machine in the Scorbit service. We refer to this unique game as a _**venuemachine**_. This is done via the Scorbit app.
* Activation and authentication of connection upon each power-up of the local game.
* Game sends heartbeat indicating it's online using a pre-defined interval.
* Game sends score and other game-related data to the API via score updates.
* Scorbit API returns information back to the game using a pre-defined format.

Please begin with the Authentication in the next section.
