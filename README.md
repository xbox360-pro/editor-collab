# Editor Collab

Build Geometry Dash levels with your friends, live. For GD 2.2081 + Geode 5.11.2.

## Quick start

### The host (whoever runs the room)
1. Open the `server` folder and double-click **Start Server** (`Start Server (Windows).bat`, `Start Server (Mac).command`, or `./start-server.sh` on Linux). Leave the window open.
   *If Windows says Python isn't installed, get it from python.org and tick "Add python.exe to PATH".*
2. In GD, open a level in the editor and tap **Collab** (top middle).
3. Tap **Host a Room**. An invite like `K7QM2X@192.168.1.20:8484` gets copied to your clipboard.
4. Send that invite to your friends.

### Friends
1. Copy the invite.
2. Make a **new empty level**, open the editor, and tap **Collab**. The invite fills in by itself.
3. Tap **Join**. That's it.

Friends don't have to set anything up, because the invite already contains the server address.

---

## Playing over the internet

The invite works out of the box for friends on **the same Wi-Fi**. For friends somewhere else, the host needs to make the server reachable. The easiest way, with no router settings, is:

1. Make a free account at **playit.gg**, download the program, and create a **TCP tunnel** to local port **8484**.
2. It gives you an address like `something.gl.joinmc.link:12345`.
3. In GD's Collab popup, type that address into the **Server** box and tap **Test**. Then **Host a Room**. The invite will use that address.

Other options: **Tailscale** / **ZeroTier** (everyone joins the same virtual network and uses the host's Tailscale IP), or forward port 8484 on your router and use your public IP.

## Getting the mod file (.geode)

GitHub builds the mod for you:

1. Create a repo on GitHub and upload everything in this folder (including the `.github` folder).
2. Open **Actions → Build Geode Mod**. It runs automatically and takes about 10 minutes.
3. Download **Build Output** from the finished run and unzip it to get `leon.editor-collab.geode`.
4. In GD, go to **Geode → ⚙ → Open Mods Folder**, drop the file in, and restart GD.

## Popup buttons

| Button | What it does |
|---|---|
| **Join** | Joins the room from the invite in the box (the paste icon fills it from your clipboard) |
| **Host a Room** | Makes a room on the server in the Server box and copies the invite |
| **Server + Test** | Only the host needs this. Test checks that the server is reachable |
| **Copy Invite** | (when in a room) copies the invite again |
| **Leave Room** | Disconnects. Your level stays as it is, so save it in GD as usual |

Mod settings (Geode → Editor Collab → ⚙) also has **Display Name**, **Sync Interval** (raise it if a huge level stutters) and **Show Other Players** (the name dots that show where everyone is looking).

## What syncs

Placing, moving, rotating, scaling, deleting, copy-paste, undo/redo, and edit-object / edit-group changes all sync. Nothing syncs while someone is playtesting.

**Not synced (yet):** level settings like colour channels, song, speed and start gamemode. Have the host set those up.

Other limits:
- If two people edit the *same* object at the same moment, one edit wins.
- Joining replaces whatever is in your level, which is why you should join from an empty one.
- Rooms have no password, so only share invites with people you want in.
- The server keeps rooms in memory. If you close it, the room ends, but everyone's level stays in their editor.

## How it works (for the curious)

Every sync tick the mod serializes each editor object and compares it with the last known version, which gives it adds, updates and deletes. Each object gets a room-wide ID. Changes go to the server over HTTP, the server puts them in order, and it hands everyone else the changes they haven't seen yet. Incoming changes are rebuilt with `createObjectsFromString` and deleted with `removeObject`.

```
mod.json                     mod info + settings
src/main.cpp                 editor hook, Collab button, status line, player markers
src/Session.{hpp,cpp}        sync engine + invites
src/CollabPopup.{hpp,cpp}    the Collab popup
server/                      relay server + double-click launchers
.github/workflows/           GitHub build for every platform
```
