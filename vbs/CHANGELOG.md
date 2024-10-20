# Changes in version 2.0 (Arelyel Krele):

!! Scorbit SDK 2.0 requires at minimum Visual Pinball X version 10.8 RC4 if using flashers to display QR Codes / Scorbit statuses. It will NOT work correctly on 10.8 RC3 or below due to the absence or buggy nature of the LoadTexture global.

## ScorbitIF Properties

* [BREAKING] Previously-public properties are now private to prevent accidental changing; use the indicated read-only replacement: 
 * bSessionActive -> SessionActive
 * bNeedsPairing -> NeedsPairing
 * bEnabled -> Enabled

* [BREAKING] Private property bRunAsynch was renamed to public property RunAsync. Set to true to make ScorbitIF call the Scorbit API asynchronously, or false to do otherwise. Generally should never change this; the class will change this automatically to the ideal setting.

* [BREAKING] Private property LogFile changed from an Array to a Dictionary as Dictionaries are more suitable for unknown numbers of indexes/keys.

* [BREAKING] Private property CachedPlayerNames(4) changed to CachedPlayer (Dictionary). This dictionary contains keys all prefixed with PlayerNumber_, and contains name (string), initials (string), and prefer_initials (boolean).
 * Use GetName, GetNameOrInitials (which is prefer_initials-aware), or GetInitials methods on the ScorbitIF class for easy retrieving of a player's name or initials.

* [BREAKING] Private property bUploadLog and public property UploadLog removed. This was replaced with a Tweak UI option defined in initScorbit "Scorbit Logs / Timelines".

* New private property dirScorbitBin which stores the defined directory path to the Scorbit binaries (.exe files)

## ScorbitIF Methods

* [BREAKING] Arguments of DoInit changed; all arguments are required:
 * MyMachineID (Integer) - Scorbit machine ID (not the venue machine ID!)
 * [New] ScorbitSDKDir (String) - Full directory path to the v1.1 Scorbit SDK / binaries (exe files); token and game log files will also be saved here
 * [Modified] QRCodeDir (String) - Full directory path to where Scorbit should save QR code images (generally PupOverlays if using PinUp)
 * MyTableVersion (String) - Current version number of the table
 * opdb (String) - The OPDB identifier for this table

* Added GetNameOrInitials method to get the name or the initials of a player (it respects the prefer_initials setting).

* Added GetInitials method to get the initials of a player.

* Added Private Function SendInstalled to support the newly-required /api/installed API endpoint. This method is called after a token is retrieved in DoInit.

* Added Private Sub HandlePlayerClaimResp and Private Sub HandleHeartbeatResp to organize Callback code and separate Heartbeat response code from Player Claim response code.

## Global variables / constants used by Scorbit

* [BREAKING] Scorbit options are no longer defined as constants / variables; they are defined in the Tweak UI via Table1.Option (See initScorbit). The exception to this rule is the new ScorbitUseFlasher and ScorbitUseLargeFlasher constants.

* New ScorbitUseFlasher constant. If true, Scorbit will use the VPX flasher named ScorbitFlasher to display QR codes and Scorbit statuses.

* New ScorbitUseLargeFlasher constant. If true, Scorbit will use the VPX flasher named ScorbitFlasherLarge to display QR codes only (hidden otherwise; also the Tweak UI has an option to not use this flasher). Great for use as a large QR code on the playfield for smaller screen resolutions.

## Callbacks

* [BREAKING] Removed ScorbitClaimQR. Instead, use Scorbit_updateQR and Scorbit_ClaimQRPinUp (note that Scorbit_updateQR also calls Scorbit_ClaimQRPinUp when PinUp is enabled when using the included default code)

* [BREAKING] Added Scorbit_SendSessionUpdate because previously the SendUpdate call was buried inside the ScorbitIF class; it should be within the table customizations section (it generally needs modified according to the variables used in a table). This new callback also allows adding other code to run on every update call by the timer.
 * This is breaking because you must use this new callback if you want the tmrScorbit timer to periodically send score updates; this is no-longer done directly in the ScorbitIF class.

* Added Scorbit_ClaimQRPinUp for PinUp-specific display or removal of QR codes

* Added Scorbit_updateQR for an organized system of deciding which QR codes / Scorbit statuses should display according to the current state of the table and Scorbit. Can be customized (e.g. adding a custom disabled status when playing a co-op game which is not supported by Scorbit at this time).

* Added Scorbit_Debug which gets called with debugging information. This allows you to use Debug.print or your own logging mechanism (e.g. to save to a text file), or to disable it entirely.

## Globally-defined Scorbit methods

* Added Sub initScorbit which handles the options, initialization, and DoInit call of Scorbit. Also handles disabling of Scorbit if disabled via the Tweak UI. This new method should be called in Table1_Init (AFTER PinUp initializes, if applicable), and on Table1_OptionEvent (when the event ID is 3).

* Added Sub ScorbitSetFlasherImages which manages the display of Scorbit statuses and QR codes on the appropriate VPX flashers. This sub should generally not be edited and is usually only useful in Scorbit_UpdateQR.

## General changes in behavior

* [BREAKING] VPX global LoadTexture is now used in GetSToken and HandleHeartbeatResp to load QR codes into VPX for use in flashers. This means the Scorbit SDK 2.0 requires at minimum VPX 10.8 RC4 to work correctly (if using the flashers).

* [BREAKING] ScorbitIF now considers any UUID with >= 16 0s or Fs as "bad" (probably a default set by your BIOS) and will fall back to the alternative UUID tool. This means for a small number of people (especially if you run VPX in a Virtual Machine), you will have to un-pair and re-pair your machines after upgrading to SDK 2.0.
 - You can easily un-pair your machines for re-pairing in the Scorbit app. Find your venue, and in the list of machines, go in the options for a machine and touch Unpair from Scorbitron (you do not have to remove the machine entirely; do not press the red button ;) ). Restart the table in VPX, and follow the pairing process as normal.

* Scorbit can be dynamically enabled (+ initialized) / disabled without restarting the game by calling initScorbit when the Tweak UI has been closed. This also can be done if Scorbit disconnects due to a network outage; just pull up the Tweak UI and then close it to restart Scorbit.

* ScorbitIF will set RunAsync to true after DoInit is finished instead of previous behavior (true on active session, false when session ends). This is to ensure the player does not jitter when playing a game without a Scorbit session running.

* Forced tmrScorbit to 2000 milliseconds on every call in case it gets changed somewhere (to prevent overflowing the Scorbit API)

* Added DO NOT REMOVE comments on lines that should never, ever be removed or modified in ScorbitIF (they protect against unwanted API calls to Scorbit)

* Various other code clean-ups were performed

* A message box error will appear if wsh.run fails when trying to get an auth token or QR code. This is usually because the SDK files are in the wrong place. The message box will instruct the user exactly where they need to go and will allow the table to continue (but with Scorbit disabled).

* SendUpdate will now prevent adding duplicate log entries to the session log which gets uploaded to Scorbit

* The script will now wait when generating a Claim QR Code (previously it did not which necessitated a delay timer or risk loading an old QR). This will cause a slight stutter one time, but generally a claim QR would never be generated in the middle of game play (the only time this will happen is if you pair the machine in the middle of a game).

# Changes in version 1.0.3 (Daphishbowl):
* Bug fix for certain MAC addresses caused Scorbit "ERROR We couldnt pair your device" when trying to pair with scorbit
* Bug fix if users turn on scorbit and start a game without pairing.
* Bugfix for users with forticlient VPN
* Generate BMP files with PNG 