# Esp8266-Frist
my frist code what c++ for Esp8266

Advanced ESP8266 OLED Eyes project with **web control panel**, animated eye moods, and buzzer effects.

## Features
- OLED eye animation (normal, happy, sad, angry, robot, wink)
- Smooth random blinking + micro eye movement
- Web panel for full live control from phone/PC
- Buzzer on **D2** with multiple melodies (startup, happy, alert)
- Brand badge and startup screen with **Ehsan Fazli**

## Main sketch
- `esp8266_oled_eyes_pro.ino`

## Pin map
- OLED MOSI: `D7`
- OLED CLK: `D5`
- OLED DC: `D4`
- OLED CS: `D8`
- OLED RST: `D3`
- Buzzer: `D2`

## HTTP endpoints
- `/` -> Web control panel
- `/mood?state=normal|happy|sad|angry|robot|wink`
- `/look?dir=up|down|left|right|center`
- `/blink`
- `/buzzer?kind=happy|alert|startup`
- `/auto/toggle`
- `/api/state`
