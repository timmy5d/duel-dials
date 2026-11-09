# Duel Dials!

A trading card game life tracker (Magic: The Gathering first, plus Yu-Gi-Oh!, Lorcana, Pokémon and Riftbound) for the **Elecrow CrowPanel 1.28" ESP32-S3 rotary display** (240×240 round GC9A01, CST816D touch, rotary knob, 5-LED ring).

## Features

- **Duel Dials logo:** the logo artwork fills the screen for 2 seconds at power-on and whenever a new game starts (any game, any mode, from any linked dial), while the LED ring sweeps through its colours. The first-player spinner starts once it's gone. Tap or press to skip it. To change the artwork, replace `assets/duel_dials_logo.webp` and run `.venv/bin/python tools/make_logo.py assets/duel_dials_logo.webp` (it writes `src/logo.cpp` and a preview at `assets/logo_240.png`).
- **Splash screen and game picker:** at power-on the five mana colours spiral into the logo (the LED ring lights up in W/U/B/R/G), then you pick a card game: **Magic**, **Yu-Gi-Oh!**, **Lorcana**, **Pokemon** or **Riftbound**, plus **DND Dice**, or **Resume** the saved game (the button underneath). Magic then asks for the format (**Commander**, **Standard**, **Archenemy**, **Emperor**). Tap how many players and *Next* (the choices depend on the game, see below; Pokemon and Emperor skip this). Last, choose **Standard** names (Player 1, Player 2 …) or **Custom**: the name editor opens for each player in turn (*Next*, then *Start* on the last one; existing custom names are filled in). The new game goes to every linked dial and the first-player spinner runs. Tap the title or the arrow, or hold the dial, to go back.
- **DND Dice:** the sixth tile on the game picker. Pick a d4, d6, d8, d10, d20 or d100 from the ring around the edge (tap it, or turn the dial), then tap the die or press the dial to roll. The die tumbles while the numbers flick and slow down, the LED ring spins in the die's colour, and the result lands with a pop. A d20 shows *Natural 20!* in gold (the LEDs glow gold) or *Critical fail* on a 1. The last four rolls show across the top. Tap the arrow or hold the dial to go back. Dice are local to each dial and don't change the game.
- **Other card games:**

  | Game | Players | Tracks | Win |
  |---|---|---|---|
  | Yu-Gi-Oh! | 2–4 | Life Points from 8000, 100 per dial click; drag to attack (starts at 1000) | Last duelist above 0 LP |
  | Lorcana | 2–4 | Lore, counting up | First to 20 lore |
  | Pokemon | 2 | Prize cards left, from 6 | Take all 6 prizes |
  | Riftbound | 2–4, or 2v2 | Points, counting up (2v2: P1+P2 vs P3+P4, team total shown) | First to 8 points (2v2: team to 11) |

  In these games the damage screen only has the score, name, colour, concede and menu, the Magic-only menu rows (Partners, Day/Night, Storm, Cmdr hits life) are hidden, and the centre shows what the number means ("of 20 lore", "prizes left", "Team A 6/11"). The victory party, linked dials, names, colours, spinner and undo work the same.
- **1–6 players** around the ring. Each seat's life total is rotated to face that player, and the centre readout turns toward whoever is focused (setting: *Face player*).
- **Formats:** Standard (20), Commander (40), **Archenemy** (Player 1 is the Archenemy at start life + 20, against 2–5 heroes, with a Schemes counter) and **Emperor** (6 players, Team A = seats 1–3, Team B = seats 4–6, emperors in the middle seats).
- **Damage and counters per player:**
  - Life (combat and non-combat damage, life loss, lifegain)
  - Commander damage from each opponent, plus a second commander per opponent when *Partners* is on. Lethal at 21. It also lowers life (setting: *Cmdr hits life*).
  - Poison (infect, toxic, poisonous, proliferate). Lethal at 10.
  - Energy, Experience, Radiation, Speed (0–4), Tickets
  - Monarch, Initiative, Concede/Rejoin
- **Player colours:** press and hold a player's name (centre of the table, or the title of their damage screen) — or use the **Color** row in their damage screen — to open a ring of 20 colours. Turn the dial to browse (the LED ring previews each one), press to choose, hold the dial to cancel. Dots mark colours other players already use. Colours are shared with linked dials, kept for rematches, and undoable; the LED ring always shows the selected player's colour.
- **Player names:** in the colour picker, tap the player's name (or use the **Name** row in their damage screen) to give them a custom name of up to 8 characters. Turn the dial to pick each letter, press to move to the next slot, tap a slot to jump to it, *Del* removes a letter, and *Done* or holding the dial saves. Leave it empty to go back to "Player N". Names show everywhere (table, attacks, commander damage, spinner, victory), sync to linked dials and carry over into rematches.
- **Icons everywhere:** each damage type, counter, role and menu item has its own vector icon (skull and crossbones for poison, radiation trefoil, a sword-and-shield for commander damage, a crown for Monarch, a horned head for the Archenemy, a laurel for emperors). The table's status line shows the focused player's damage as icon + number.
- **Touch-first damage screen:** a player's damage and counters are a grid of round tiles showing each icon and value (commander damage tinted in each opponent's colour); tap one to open it, swipe for more. The editor and attack screens have big − / + buttons that repeat when held.
- **Damage editor:** picking any damage or counter opens a full-screen editor with that icon large and faint behind the number. Turn the dial to change it. A ring around the rim fills toward lethal (10 poison, 21 commander).
- **Drag to attack:** drag your finger from one player's seat to another's to open an attack screen. Pick combat damage (sword), commander damage (shield, A/B with partners) or poison/infect (skull), toggle lifelink (heart), set the amount with the dial, and press to apply. The whole attack is one undo step.
- **First-player spinner:** every new game (and *Who goes first* in the menu) spins an arrow around the table that slows down and lands on a random player; the LED ring follows it. In Archenemy the Archenemy always goes first, so the spinner is skipped.
- **Home:** press the screen three times quickly to go back to the game picker from anywhere (except while typing a name). The current game is kept, so *Resume* takes you straight back. On the table, picker and victory screens the first two presses wait a moment, so a triple press never undoes or skips ahead.
- **Quick reset:** press the screen in (it's the dial's button) and keep it pressed for 3 seconds. A red ring counts down from about 1 second; let go before it fills to cancel. At 3 seconds the dial goes back to the game picker. Resting a finger on the glass no longer resets; it opens the menu when you let go.
- **Linked dials:** several dials can share one game over ESP-NOW (direct radio, no Wi-Fi network needed). In the game menu, open *Connectivity* and set the same room (1–9) on each dial to join the same session: life, damage, counters, attacks, new games, the first-player spinner and undo all appear on every dial. Set *This dial is player* to your seat; seats that have a dial show a small dial icon, and the table shows a chain icon with the number of linked dials.
- **Game-wide:** turn counter, Day/Night (a big sun or moon appears behind the life total), storm count, d20, d6, coin flip, random first player.
- **Knockout celebrations:** when a player is knocked out mid-game, every linked dial plays a ~3.5 s animation themed to how they went out: a wobbling green skull and crossbones with rising bubbles (poison), a shield slashed in half that shatters in the attacker's colour (commander damage), a heart that cracks apart and spills red drops (life), or a waving white flag (concede). The rim and LED ring pulse in the matching colour, then the dial returns to what it was showing. Tap or press to skip; undoing the knockout cancels it; if it ends the game, the victory party plays instead.
- **Victory celebration:** when the game is decided, confetti bursts over turning light rays in the winner's colour, with a gold crown and the winner's name, while the LED ring chases and flashes gold. Linked dials celebrate together. After a moment a **Play again?** button appears: press the dial (or tap it) to start a new game with the same format, players and life, and the first-player spinner runs on every linked dial. Double-press to undo the winning hit instead.
- **Automatic knockouts** (life ≤ 0, 10 poison, 21 commander damage, concede) and win detection for each format (last player standing, "Heroes win", "Team A wins").
- **Undo** (40 steps; quick edits are grouped into one step).
- **Light and dark themes**, brightness, screen-off timer, dial direction, LED ring in the focused player's colour.
- The game and settings are saved to flash and survive power-off.

## Controls

| Where | Input | Action |
|---|---|---|
| Table | Turn dial | Change the focused player's life |
| Table | Press dial | Focus the next player |
| Table | Double-press dial | Undo |
| Victory | Press dial / tap *Play again?* | New game with the same setup (all linked dials) |
| Victory | Hold dial / tap elsewhere | Look at the final board (press the dial there to bring the prompt back) |
| Victory | Double-press dial | Undo the winning hit |
| Table | Tap a seat on the ring | Focus that player |
| Table | Tap the centre / hold dial | Open the player's damage and counter screen |
| Table | Touch and hold, then release | Game menu |
| Table | Touch and hold the player's name | Colour picker for that player |
| Colour picker | Turn dial / tap a swatch | Browse colours (LEDs preview) |
| Colour picker | Press dial / tap the centre | Choose |
| Colour picker | Hold dial | Cancel |
| Colour picker | Tap the name | Rename the player |
| Name editor | Turn dial / press | Change letter / next slot |
| Name editor | Tap Done or hold dial | Save (empty = default name) |
| Anywhere | Press the screen 3 times quickly | Home: game picker (Resume returns to the game) |
| Anywhere | Press the screen in and hold for 3 seconds | Back to the game picker |
| Game picker | Tap a format / turn dial + press | Choose Commander, Standard, Archenemy, Emperor or Resume |
| Game picker | Tap a number, then *Next* (or turn dial + press) | Number of players |
| Game picker | Tap *Standard* or *Custom* (or turn dial + press) | Player names; Custom walks through each player's name, then starts |
| Game picker | Tap the title or arrow / hold dial | Back to the formats |
| Table | Drag from one seat to another | Attack: assign damage from that player |
| Attack | Turn dial | Amount |
| Attack | Tap a chip | Damage type / lifelink |
| Attack | Press dial / tap the number | Apply |
| Attack | Hold dial / tap the header | Cancel |
| Damage tiles | Tap a tile | Open that damage/counter (Monarch and Initiative toggle) |
| Damage tiles | Swipe left/right | More tiles (page dots at the bottom) |
| Damage tiles | Turn dial / press | Move the highlight / open it |
| Damage tiles | Tap the name at the top / hold dial | Back to the table |
| Value editor, Attack | Tap − / + (hold to repeat, speeds up) | Change the value |
| Value editor | Tap *Done* | Finish |
| Lists | Turn dial | Scroll, or change the value while a row is highlighted blue |
| Lists | Press dial / tap row | Edit or confirm a row, toggle, or run an action |
| Lists | Hold dial / tap the title | Back |
| Damage editor | Turn dial | Change the value |
| Damage editor | Press dial / tap | Done |

*New game* takes two presses. Format, Players and Start life apply when you start a new game.

## Linking dials

1. On each dial, open the game menu (touch and hold the screen, then let go) and choose **Connectivity**, the second row.
2. Set **Room** to the same number (1–9) on every dial with − / + (or turn the dial). The room is joined when you tap **Done**, so scrolling past other rooms doesn't join them. *Off* unlinks the dial.
3. Set **This dial is player** to the seat the dial belongs to (P1–P6). It changes straight away, and the dial then opens on that player.
4. The row of dots shows the table's seats: filled where a dial sits, ringed for this one, with the number of other dials linked underneath.
5. The first dial in a room keeps its game. Dials that join later take on the room's game.

Press the dial to switch between the Room and Player rows. Tap the title, tap *Done* or hold the dial to go back to the menu. The menu row shows the current setting, for example *R1 P2*.

How it works: every change is broadcast as the full game state (about 180 bytes) with a revision number. The newest revision wins, and each dial repeats its state every 2 seconds, so a dial that reboots or comes back in range catches up. Settings such as theme, brightness and seat stay per dial.

Limits: two edits made on different dials within about 30 ms of each other can lose one of them. Rooms aren't encrypted, so two nearby tables should use different room numbers.

## Phone companion app

Any dial can host a companion web app for an iPhone or Android phone. Nothing to install: the dial makes its own Wi-Fi network and serves the page.

1. On the dial: game menu → **Connectivity** → **Phone** → **Turn on**. The setting is remembered across restarts.
2. Scan the QR code with the phone's camera to join the Wi-Fi (**dueldial**, password **dualdial**). Every dial uses the same name, so turn the phone app on for one dial at a time.
3. The app usually opens by itself (the dial acts as a sign-in page). If not, tap the QR code on the dial to show the app link and scan that, or open **http://192.168.4.1** in the browser. In Safari, *Share → Add to Home Screen* makes it look like an app.

The dial keeps linking with the other dials while the phone is connected, so the phone sees and changes the whole table's game.

| Tab | What it does |
|---|---|
| **Table** | Every player's life (or LP, lore, prizes, points) with big − / + buttons, or tap the number for a keypad to enter damage or gain. *Damage, counters & more* opens commander damage from each opponent, poison and the other counters, Monarch, Initiative, Schemes, concede, and name and colour. Turn, Day/Night, Storm, Undo and *Who goes first* sit at the top. |
| **New game** | Pick the game and format, players, starting life, partners, and each player's name and colour, then start it on every linked dial (logo and spinner included). |
| **Log** | A running list of what happened this game, newest first: life changes, commander damage, counters, knockouts, monarch, turns and the winner, from any dial or the phone. Undo is at the top. |
| **Dial** | Room and seat, which seats have a dial, and the hosting dial's own settings: theme, brightness, LED ring, screen-off timer, dial direction, readout facing and *Commander damage hits life*. |

The phone page lives in `web/app.html`. The build packs it into `src/webapp.h` automatically (`tools/make_web.py`). The API is `GET /state` (JSON) and `POST /act` (for example `a=life&p=0&d=-3`).

## Build and flash

```bash
python3 -m venv .venv && .venv/bin/pip install platformio esptool pyserial
.venv/bin/pio run -t upload
```

If the upload can't connect, hold **BOOT**, tap **RESET** (or replug USB), then release BOOT and try again.

The boards' original firmware is in `backup/`: `factory_16MB.bin` (dial 1), `factory_16MB_dial2.bin` (dial 2), `factory_16MB_dial3_EBD0.bin` and `factory_16MB_dial4_CA68.bin`. To restore it:

```bash
esptool.py --chip esp32s3 write_flash 0x0 backup/factory_16MB.bin
```

## Debug tools

The firmware has a serial console for driving the UI from the host:

```bash
python3 tools/dial.py /dev/cu.usbmodem* r3 "t 120 120" shot:screen.png
python3 tools/log.py /dev/cu.usbmodem*        # reset and print the boot log
cd tools && python3 duo.py PORT1 PORT2 1:r3 2:shot:b.png 1:n   # drive two linked dials
```

Commands: `n` link status, `g` icon gallery, `r`/`l` (dial ± detents, e.g. `r5`), `c` click, `d` double-click, `L` long-press, `t X Y` tap, `h X Y` hold and release, `P MS [X Y]` finger held for MS (at X,Y), `T` triple press, `R ROOM SEAT` set the link room and this dial's seat, `J` print the phone API's JSON state, `H` 3-second reset hold, `B MS` press the screen in for MS milliseconds, `m SX SY EX EY` drag (`M` = drag without releasing), `shot:FILE.png` screenshot, `wait:SECONDS`.

## Layout

- `src/board.h`: pin map and display driver
- `src/input.*`: encoder, button and touch handling
- `src/game.*`: rules, formats, win detection, undo, saving
- `src/ui.*`: screens, themes, rendering, LEDs and backlight
- `src/icons.*`: the vector icon set
- `src/logo.*`: the Duel Dials logo as a 240×240 image (generated by `tools/make_logo.py`)
- `src/net.*`: linking dials over ESP-NOW (and the phone app's Wi-Fi access point)
- `src/web.*`: the phone app's web server, JSON API and game log
- `web/app.html`: the phone app
