# Editor Collab

Build Geometry Dash levels with your friends, live. There's no setup: one person hosts and shares a room code, like Globed. For GD 2.2081 + Geode 5.11.2.

## How to use

1. Open a level in the editor.
2. Tap **Collab**. It's at the top middle of the editor, and also in the bottom-right corner of the editor pause menu.
3. **Hosting:** tap **Host a Room**. A 6-letter code (like `K7QM2X`) gets copied, so send it to your friends.
4. **Joining:** make a **new empty level**, tap **Collab**, type or paste the code, and tap **Join**.

Everything syncs live: placing, moving, rotating, scaling, deleting, copy-paste, undo, and edit-object/group changes. Coloured name dots show where everyone is looking. Save in GD as usual.

> The first room after a quiet period can take up to a minute to connect while the free server wakes up. After that it's instant.

**Not synced (yet):** level settings like colours, song, speed and start gamemode.

## Install

Download **Build Output** from the latest successful run under **Actions** (you need to be signed in to GitHub), unzip it, and drop `leon.editor-collab.geode` into GD's **Geode → ⚙ → Open Mods Folder**. Then restart GD.

---

## Running the global server (one-time, for the repo owner)

Everyone connects to one free server that runs on [Render](https://render.com):

[![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/xbox360-pro/editor-collab)

1. Click the button and sign in to Render with GitHub. The free plan is fine, and you don't need a card.
2. Click **Deploy**. When it's done, the service URL should be `https://editor-collab-xbox360pro.onrender.com`.
3. If Render gave it a different URL, change `GLOBAL_SERVER` in `src/Session.hpp` to match and push. The mod rebuilds automatically.

On the free plan the server sleeps after 15 minutes with nobody connected and wakes up on the next connection. Rooms only live in memory, so a room ends when everyone leaves, but everyone's level stays in their editor.

## Self-hosting instead (optional)

You can also run the server on your own PC with `server/Start Server (Windows).bat` (needs Python). Then in the mod's settings turn on **Use My Own Server** and set the address. Rooms you host that way give out invites like `K7QM2X@192.168.1.20:8484`, and friends paste the whole thing into the code box.

## Files

```
mod.json                     mod info + settings
src/main.cpp                 Collab buttons, status line, player markers
src/Session.{hpp,cpp}        sync engine (GLOBAL_SERVER lives in Session.hpp)
src/CollabPopup.{hpp,cpp}    the Collab popup
server/                      the relay server + double-click launchers
render.yaml                  one-click Render deploy
.github/workflows/           builds the mod for Windows, Mac and Android
```
