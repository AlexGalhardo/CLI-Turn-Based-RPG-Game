# Opening: language, title screen, new run (difficulty, name, vocation, auto-equip) and the first fight.
Output docs/media/start.gif
Source scripts/gameplay/settings.tape

Hide
Type "clear && rpg-golang --seed 42"
Enter
Sleep 1.5s
Show

Sleep 1.5s
Type "1"
Sleep 2s
Type "2"
Sleep 2s
Type "2"
Sleep 1.5s
Type@200ms "Alex"
Sleep 800ms
Enter
Sleep 2s
Type "1"
Sleep 2s
Type "1"
Sleep 2.5s
Type "0"
Sleep 4s
