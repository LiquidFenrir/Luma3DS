# Luma3DS Update Detection Fork
*Noob-proof (N)3DS "Custom Firmware"*

## What it is

This is a modified version of Luma3DS that takes advantage of the background process "Rosalina" in order to automatically check for updates to the Luma3DS CFW. This fork is *not* intended to be used yet, though it is in a usable state as of right now.
---

## Compiling

Same as regular Luma

## Licensing

This software is licensed under the terms of the GPLv3.  
You can find a copy of the license in the LICENSE.txt file.

# Details

On boot, this waits for a network connection then checks the most recent luma version via the GitHub JSON API. It then compares it to the current version, and sends a notification if different.

# Credits
Same as regular Luma, plus additionally a lot of credit to LiquidFenrir for working on the network functions for this (as well as generally improving my code). Credit to ihaveamac for some initial debugging help. And credit to a few more people, you know who you are.
