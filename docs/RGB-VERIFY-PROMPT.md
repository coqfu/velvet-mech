# RGB verification prompt (paste into the reviewing model)

Copy everything below the line into the other model. It opens with an explicit
task, so sending the file alone is enough. If that model still answers with a
menu of options instead of starting, reply with one line:

> Run the RGB verification now, starting with Test 1. If you cannot reach
> `127.0.0.1:8765`, print the command and ask me to run it, then continue.

---

You are verifying the RGB lighting of a hand-built keyboard. The board cannot be
seen by you: every colour claim must come from the person in front of it. Your
job is to run the tests below through the local HTTP service and ask exactly one
short question after each one.

## Your task (do this now, in order)

Run Tests 1-7 in the order written. After each test send **its single
question**, wait for the answer, then decide pass/fail and only then continue.
Do not summarise the document back, do not ask what to do with it, and do not
skip a test because it looks similar to another one.

- You need a terminal on the same machine as the keyboard (service on
  `http://127.0.0.1:8765`).
- If you cannot reach that address, do not stall: print the command block for
  the current test, ask the user to run it, ask the same single question, and
  carry on exactly as if you had run it yourself. Say once, at the top of the
  report, that the commands were run by the user.
- If a test fails, stop that test, record the failure, and continue with the
  next test. Do not attempt repairs, rebuilds or flashes.
- Report as you go: command sent, answer received, pass/fail. Finish with the
  restored state and what is still unverified.

Start with Test 1 and Question 1.

## Hardware facts (do not overstate these)

- Board: CH555, 60 KiB code flash, 8 KiB xdata, USB `1209:c555` "Velvet Mech".
- 63 key LEDs (64% layout, 5 rows) plus 6 rear LEDs in one strip = **69 LEDs**.
- Key rows are 5: row 0 = Esc…Backspace, row 1 = Tab/Q…, row 2 = Caps/A…,
  row 3 = LShift/Z…, row 4 = LCtrl/Space/Fn….
- All 63 keys light individually; there is **no** per-key addressability on the
  six rear LEDs — they share one driver and show one colour.
- Key effect and rear light are independent. In **Direct** mode the six rear
  LEDs show the average of the six host underglow colours.
- Brightness is a single master level for all 63 keys. The rear light has its
  own level, its own colour and its own animation.
- Firmware: `build/keyboard.hex`, md5 `1f4d3a861c93bb0d91310d352efed021`.
  The top-row mapping fix is in this build (see below).

## Recent fix you are validating

The 14 LEDs of the top key row are not on channel 1 like rows 1-4; they are
wired to the channels the renderer used as back-light spares (0, 6, 7). Before
the fix the row was painted with the rear-light colour every frame: dark with
the rear light off, magenta when the rear colour was magenta. The firmware now
mirrors the row-0 colour into those three channels (only the wired one is
visible) and the rear colour can no longer overwrite it.

**Your first job is to prove that the top row now follows every key effect, at
the same brightness and the same animation phase as the other four rows.**

## How to talk to the board

Service: `http://127.0.0.1:8765` (already running; do not kill it).

| endpoint | use |
|---|---|
| `GET /api/status` | connection + firmware diagnostics (`diag` object) |
| `GET /api/state` | current lighting state the host believes |
| `POST /api/apply` | set an effect, or send a full Direct frame |
| `POST /api/config` | rear light / calibration / debounce (no save unless asked) |
| `POST /api/save` | **writes data flash** — do not call during verification |
| `POST /api/boot-preview` | replay the start-up animation once |

Rule: never call `/api/flash`, never rebuild, never edit source. If a firmware
change is genuinely needed, write down what and why and stop.

### Known operational quirks

- After a reset the board enumerates before it has loaded its settings. An
  apply sent in the first second can be overwritten by that load. If
  `diag.directActive` is wrong, wait 1 s and send the same request again.
- `diag.fps` is occasionally nonsense for a sample; `frames` must keep rising.
- A Direct frame needs `effect: -1, mode: 6` and exactly 69 colours.

## Honesty rules

1. Ask one question at a time. Never combine two observations.
2. Never report a row as correct because the firmware "should" write it. Only
   the user's answer is evidence.
3. If the answer is vague ("looks fine"), ask for the specific colour or the
   specific key.
4. Never invent a diagnostic value; quote what `/api/status` returned.
5. Do not offer any control the firmware lacks (per-LED rear colours, key
   brightness per row, effect speeds outside 1-8, …).

## Test 1 — row map (do this first)

Send a Direct frame with one colour per key row, rear light **off**:

```bash
python3 - <<'PY'
import json, urllib.request
def post(p, b):
    r = urllib.request.Request("http://127.0.0.1:8765"+p, data=json.dumps(b).encode(),
                               headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r).read().decode())
post("/api/config", {"backOn": 0})
keys = json.load(open("host/map.json"))["keys"]
row = {0:[255,0,0], 1:[0,255,0], 2:[0,0,255], 3:[255,255,255], 4:[255,128,0]}
post("/api/apply", {"lighting": {"effect": -1, "mode": 6, "val": 254, "speed": 112,
                                 "colours": [row[k["row"]] for k in keys] + [[0,0,0]]*6}})
PY
```

**Question 1:** "Read the keyboard from the top down. What colour is each of the
five key rows?"

- Pass: red, green, blue, white, orange (top to bottom), rear strip dark.
- If the top row is dark, the row-0 mirror is not reaching the hardware — stop
  and report it. Do not guess a second fix.
- If any other row is out of order, the whole row→channel map is shifted; report
  the actual sequence, do not "correct" it by hand.

## Test 2 — top row follows brightness

Apply effect 1 (Solid) at hue 20, sat 215, val 254. Ask the user to press
**Fn+↓ three times**, then **Fn+↑ three times**.

**Question 2:** "As you dim, does the top row dim by the same amount as the row
below it, or does it stay brighter/darker?"

- Pass: same brightness change on all five rows.
- Failure means row 0 is not using the master dimmer; report which way it drifts.

## Test 3 — top row follows motion

Apply effect 3 (Rainbow), speed 112.

**Question 3:** "Is the top row changing colour with the other rows, and does it
change at the same moment, or does it lag/lead?"

Then effect 13 (Strobe).

**Question 4:** "Does the top row flash on and off with the other rows, or does
it stay lit / stay dark?"

- Pass: same phase, no lag.
- A row that is one animation step behind or ahead is a clock/mapping problem,
  not a brightness problem — report it as such.

## Test 4 — top row reacts to keys

Apply effect 6 (Reactive). Ask the user to press `Esc`, then `1`, then `Q`.

**Question 5:** "When you press Esc, does the Esc key itself light up? Then does
the whole top row flare when you press 1?"

- Pass: the pressed key lights and the row participates like the others.

## Test 5 — rear light is independent

1. `POST /api/config {"backOn":1,"backLevel":127,"backRgb":[255,0,0],"backMode":0}`
   with a Solid key effect on screen.
2. **Question 6:** "Is the strip behind the keyboard red, and is the top key row
   still the key colour (not red)?"
   - Pass: strip red, keys unchanged. A red top row would mean the old
     spare-channel bleed is back.
3. `POST /api/config {"backOn":0}`.
4. **Question 7:** "Did only the strip go dark? Are all five key rows still
   lit?"
5. Ask for **Fn+B** twice.
   **Question 8:** "Did Fn+B switch only the strip, with the keys unchanged?"
   - Pass for both.

## Test 6 — 69 LEDs in Direct (host painting)

Send a Direct frame where every key gets its own colour and the six underglow
LEDs are white:

```bash
python3 - <<'PY'
import json, urllib.request
def post(p, b):
    r = urllib.request.Request("http://127.0.0.1:8765"+p, data=json.dumps(b).encode(),
                               headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r).read().decode())
post("/api/config", {"backOn": 1, "backLevel": 127})
keys = json.load(open("host/map.json"))["keys"]
cols = []
for i, _k in enumerate(keys):
    cols.append([(i*37) % 256, (i*91) % 256, (i*151) % 256])
cols += [[255, 255, 255]] * 6
post("/api/apply", {"lighting": {"effect": -1, "mode": 6, "val": 254,
                                 "speed": 112, "colours": cols}})
PY
```

**Question 9:** "Is the board a mosaic of different colours — no row left dark,
no two neighbouring keys stuck on the same colour for a long run?"

**Question 10:** "Is the rear strip white (the average of the six white
underglow LEDs)?"

- Pass: 63 distinct-ish keys, strip white.
- If the top row is again the *rear* colour, the mirror was lost.

Then set only the six underglow LEDs to red, keys black, and ask
**Question 11:** "Is the strip red and every key dark?"

## Test 7 — custom picture and save

1. `POST /api/apply` with `effect: 34` in `lighting` and a `custom` array of 63
   colours at the top level (the service sends it as 15-key packets), e.g. the
   mosaic from Test 6 with the last six entries dropped:

```bash
python3 - <<'PY'
import json, urllib.request
r = urllib.request.Request("http://127.0.0.1:8765/api/apply",
        data=json.dumps({"lighting": {"effect": 34, "val": 254, "speed": 112},
                         "custom": [[(i*37) % 256, (i*91) % 256, (i*151) % 256]
                                    for i in range(63)]}).encode(),
        headers={"Content-Type": "application/json"})
print(urllib.request.urlopen(r).read().decode())
PY
```

Ask **Question 12:** "Are the keys showing your picture with no row black?"
2. Only if the user consents to a flash write: `POST /api/save`, then ask them
   to unplug and replug, and **Question 13:** "After the replug is the same
   picture back, with the top row included?"

## Final state to restore

```bash
curl -s -X POST http://127.0.0.1:8765/api/apply \
  -H 'Content-Type: application/json' \
  -d '{"lighting":{"effect":1,"hue":20,"sat":215,"val":254,"speed":112}}'
curl -s -X POST http://127.0.0.1:8765/api/config \
  -H 'Content-Type: application/json' -d '{"backOn":0}'
```

## Report format

For each test: the command sent, the exact answer received, pass/fail, and — if
failed — which channel or row the evidence points at. Do not claim a test passed
without an answer from the user. Finish with the restored state and one sentence
on what is still unverified.
