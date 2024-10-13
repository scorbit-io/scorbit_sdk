# ZSCO: SCORBIT Interface v2.0.beta5

- Written by @arelyelkrele
- Based on original Dev Kit by @daphishbowl

[Changelog](CHANGELOG.md)

***CAUTION!!!*** If you re-distribute the contents of the SDK folder, avoid including these files as they contain sensitive info:
QRclaim.bmp, QRclaim.png, QRcode.bmp, QRcode.png, all .dat files (e.g. sToken_number.dat).
Don't remove them from your own SDK folder (or you might have to re-pair your machines) but ensure they're not in the folder you
distribute.

## To integrate Scorbit into your own table:

0. Contact the Scorbit team (such as in the Scorbit Discord server) to get (or create) the Machine ID for your table.

1. Create a timer named tmrScorbit; it should be disabled by default. Interval does not matter (it is set to 2 seconds in the code).

2. If you want to use a VPX flasher for Scorbit QR code and statuses:
   - Set ScorbitUseFlasher to true. Create a square flasher named ScorbitFlasher. Set visibility off, opacity 100%, amount 100%. 
   - Place this where you want Scorbit QR codes and statuses to appear, such as on the apron. Note that it will almost always show.
   - Set height to 2 (or make it 2 units higher than the highest object under it).

3. If you want to use a large VPX flasher to display large Scorbit QR codes (easier to scan for lower-res displays):
   - Set ScorbitUseLargeFlasher to true. Create a square flasher named ScorbitFlasherLarge. Set visibility off, opacity 100%, amount 100%. 
   - Make this flasher rather large (suggested you square-scale it by a factor of 4). Place on the middle of the playfield. 
   - Set height to 2 (or make it 2 units higher than the highest object under it). It will hide itself when a ball is in play.
   - This will display only QR codes and only when the option "Scorbit QR Large" is enabled or ScorbitUseFlasher is false.

4. If ScorbitUseFlasher is true (step 2), in the VPX images manager, import these images (from the SDK):
   ScorbitNotReady, ScorbitError, ScorbitClaimInApp, ScorbitClaimed, ScorbitDisabled, and ScorbitReady.

5. In the VPX sound manager, import these sounds:
   scorbit_detected_2, scorbit_detected_2b, scorbit_login.

6. If your table has a PinUp pack, copy (and optionally customize) these images to your PUP pack (pupoverlays typically):
   QRcodeB, QRcodeS.

7. Modify the DoInit call in the initScorbit Sub as follows:
   - Replace 0 with your Machine ID from Scorbit
   - Replace TablesDirectory & "\ScorbitSDK_2_0" with the full path to the Scorbit SDK and binaries
   - Replace puplayer.getroot & "\PupOverlays" with the full path to where the generated QR Code images should be saved
     (Should use PupOverlays if the table uses PinUp, otherwise can be anything like TablesDirectory & "\" & cGameName)
   - Replace "1.0.0" with the version number of your table
     (Ideally you should define your table version in a constant at the top of your table script)
   - Replace OPDB-ID with your table on OPDB - eg: https://opdb.org/machines/2103 
     (original vpins will have a different OPDB ID provided by Scorbit)

8. Customize these functions as necessary, and then have them get called as indicated:
   - initScorbit
     - in your Table1_Init Sub (After PUP is initialized, if applicable)
     - in your Table1_OptionEvent Sub (when eventId is 3)
   - StartSession
     - When a game starts (e.g. ResetForNewGame)
   - StopSession
     - When the game is over (e.g. EndOfGame)
   - StopSession2
     - When the game is cancelled (e.g. Table1_Exit, or a Slam Tilt)
   - SendUpdate
     - When Score Changes (e.g. AddScore) 
     (this is optional but not recommended for vpins; see Scorbit_SendSessionUpdate and ideally use that instead)
   - SetGameMode
     - When different game events happen like starting a mode, MB etc. 
     (ScorbitBuildGameModes helper function shows you how)

9. Customize Callbacks:
   - Scorbit_Paired
     - Called when machine is successfully paired.
   - Scorbit_PlayerClaimed
     - Called when player is claimed.
   - Scorbit_SendSessionUpdate
     - Called when it is time to send a score update to Scorbit
   - Scorbit_ClaimQRPinUp
     - Call when we want to show/hide the claim QR code on PinUp
     (generally should be called by Scorbit_UpdateQR)
   - Scorbit_updateQR
     - Call/called when we should re-calculate what to display for Scorbit statuses and QR codes
     (any changes to game in progress status, change in which player is up, or when a ball hits / unhits the plunger lane trigger)
   - Scorbit_Debug
     - All debug information gets called by this Sub. You can customize this to use debug.print, your choice of a logger, or 
     nothing at all.

10. Delete these comments inside your table's script and instruct in a comment instead to refer to this file (Scorbit_2_0.vbs.txt) 
    in the SDK for implementation instructions and script. That way authors don't accidentally integrate customizations specific to your table.

11. MOVE YOUR CAR!!!

**PRO TIP:** Find comments containing "NB:"; these are places of the code you will likely need to edit for your table